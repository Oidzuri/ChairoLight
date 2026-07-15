/*
 * calculations.cpp
 *
 *	Created on: 23.07.2012
 *		Project: Lightpack
 *
 *	Copyright (c) 2012 Timur Sattarov
 *
 *	Lightpack a USB content-driving ambient lighting system
 *
 *	Lightpack is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Lightpack is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program.	If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "calculations.hpp"
#include <stdint.h>
#include <algorithm>
#include <cmath>
#ifdef __SSE4_1__
#include <immintrin.h>
#endif // ifdef __SSE4_1__

#define PIXEL_FORMAT_ARGB 2,1,0 // channel positions in a 4 byte color
#define PIXEL_FORMAT_ABGR 0,1,2
#define PIXEL_FORMAT_RGBA 3,2,1
#define PIXEL_FORMAT_BGRA 1,2,3


namespace {
	constexpr const uint8_t bytesPerPixel = 4;
	constexpr const uint8_t pixelsPerStep = 4;

	struct ColorValue {
		int r, g, b;
	};

	struct PerceptualTuning {
		double meanWeight;
		double rmsWeight;
		double vividScale;
		double vividLimit;
		double highlightKnee;
		double highlightStrength;
		double warmProtection;
	};

	struct RegionStats {
		double meanR;
		double meanG;
		double meanB;
		double rmsR;
		double rmsG;
		double rmsB;
		double meanLuminance;
		double luminanceContrast;
		double saturation;
		double warmBias;
		double brightRatio;
		double darkRatio;
		double neutralRatio;
		double colorfulness;
		double vividR;
		double vividG;
		double vividB;
		bool hasVividColor;
	};


#define PIXEL_INDEX(_channelOffset_,_position_) (index + _channelOffset_ + (bytesPerPixel * _position_))
#define PIXEL_CHANNEL(_channel_,_position_) (buffer[PIXEL_INDEX(offset##_channel_,_position_)])
#define PIXEL_R(_position_) (PIXEL_CHANNEL(R,_position_))
#define PIXEL_G(_position_) (PIXEL_CHANNEL(G,_position_))
#define PIXEL_B(_position_) (PIXEL_CHANNEL(B,_position_))

	template<uint8_t offsetR, uint8_t offsetG, uint8_t offsetB>
	static ColorValue accumulateBuffer(
		const int* const buff,
		const size_t pitch,
		const QRect& rect) {
		const unsigned char* const buffer = (const unsigned char* const)buff;

		ColorValue color{0,0,0};

		const int delta = rect.width() % pixelsPerStep;
		if (rect.width() >= pixelsPerStep)
			for (int currentY = 0; currentY < rect.height(); currentY++) {
				for (int currentX = 0; currentX < rect.width() - delta; currentX += pixelsPerStep) {
					const size_t index = pitch * bytesPerPixel * (rect.y() + currentY) + (rect.x() + currentX) * bytesPerPixel;
					color.r += PIXEL_R(0) + PIXEL_R(1) + PIXEL_R(2) + PIXEL_R(3);
					color.g += PIXEL_G(0) + PIXEL_G(1) + PIXEL_G(2) + PIXEL_G(3);
					color.b += PIXEL_B(0) + PIXEL_B(1) + PIXEL_B(2) + PIXEL_B(3);
				}
			}
		for (int currentX = rect.width() - delta; currentX < rect.width(); ++currentX) {
			for (int currentY = 0; currentY < rect.height(); ++currentY) {
				const size_t index = pitch * bytesPerPixel * (rect.y() + currentY) + (rect.x() + currentX) * bytesPerPixel;
				color.r += PIXEL_R(0);
				color.g += PIXEL_G(0);
				color.b += PIXEL_B(0);
			}
		}
		const size_t count = rect.height() * rect.width();
		color.r = (color.r / count) & 0xff;
		color.g = (color.g / count) & 0xff;
		color.b = (color.b / count) & 0xff;
		return color;
	};

	/*
	 * Ambient-light color estimator.
	 *
	 * A normal RGB mean is cheap, but it makes mixed scenes muddy and lets dark
	 * pixels hide small highlights. We blend that mean with a channel RMS
	 * (highlight energy), then gently steer it towards a saturation-weighted
	 * color. The original zone luminance is retained, so a tiny bright object
	 * does not turn the entire wall into a full-brightness lamp.
	 *
	 * Sampling is capped at roughly 4096 pixels per LED zone. That keeps the
	 * cost predictable for large capture rectangles while still covering the
	 * complete area uniformly.
	 */
	template<uint8_t offsetR, uint8_t offsetG, uint8_t offsetB>
	static RegionStats analyzeRegion(
		const unsigned char* const buffer,
		const size_t pitch,
		const QRect& rect) {

		if (buffer == nullptr || rect.width() <= 0 || rect.height() <= 0)
			return RegionStats{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false};

		constexpr double maxSamples = 4096.0;
		const double area = static_cast<double>(rect.width()) * rect.height();
		const int sampleStep = std::max(1, static_cast<int>(std::ceil(std::sqrt(area / maxSamples))));

		uint64_t sumR = 0, sumG = 0, sumB = 0;
		uint64_t sumR2 = 0, sumG2 = 0, sumB2 = 0;
		uint64_t vibrantR = 0, vibrantG = 0, vibrantB = 0;
		uint64_t vibrantWeight = 0;
		uint64_t chromaSum = 0;
		uint64_t luminanceSum = 0;
		uint64_t luminanceSum2 = 0;
		uint64_t warmSum = 0;
		uint64_t brightCount = 0;
		uint64_t darkCount = 0;
		uint64_t neutralCount = 0;
		uint64_t count = 0;

		for (int y = rect.top(); y <= rect.bottom(); y += sampleStep) {
			for (int x = rect.left(); x <= rect.right(); x += sampleStep) {
				const size_t index = pitch * static_cast<size_t>(y) + bytesPerPixel * static_cast<size_t>(x);
				const int r = buffer[index + offsetR];
				const int g = buffer[index + offsetG];
				const int b = buffer[index + offsetB];

				sumR += r; sumG += g; sumB += b;
				sumR2 += r * r; sumG2 += g * g; sumB2 += b * b;

				const int maximum = std::max(r, std::max(g, b));
				const int minimum = std::min(r, std::min(g, b));
				const int chroma = maximum - minimum;
				const int luminance = (54 * r + 183 * g + 19 * b) >> 8;
				const int warm = std::max(0, ((r + g) / 2) - b);
				const uint64_t weight = static_cast<uint64_t>(chroma) * (luminance + 16);

				vibrantR += weight * r;
				vibrantG += weight * g;
				vibrantB += weight * b;
				vibrantWeight += weight;
				chromaSum += chroma;
				luminanceSum += luminance;
				luminanceSum2 += static_cast<uint64_t>(luminance) * luminance;
				warmSum += warm;
				brightCount += luminance >= 190 ? 1 : 0;
				darkCount += luminance <= 52 ? 1 : 0;
				neutralCount += chroma <= 26 ? 1 : 0;
				++count;
			}
		}

		if (count == 0)
			return RegionStats{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false};

		return RegionStats{
			static_cast<double>(sumR) / count,
			static_cast<double>(sumG) / count,
			static_cast<double>(sumB) / count,
			std::sqrt(static_cast<double>(sumR2) / count),
			std::sqrt(static_cast<double>(sumG2) / count),
			std::sqrt(static_cast<double>(sumB2) / count),
			static_cast<double>(luminanceSum) / count,
			std::sqrt(std::max(0.0, static_cast<double>(luminanceSum2) / count
				- std::pow(static_cast<double>(luminanceSum) / count, 2.0))),
			static_cast<double>(chromaSum) / (count * 255.0),
			static_cast<double>(warmSum) / (count * 255.0),
			static_cast<double>(brightCount) / count,
			static_cast<double>(darkCount) / count,
			static_cast<double>(neutralCount) / count,
			static_cast<double>(chromaSum) / (count * 255.0),
			vibrantWeight > 0 ? static_cast<double>(vibrantR) / vibrantWeight : 0.0,
			vibrantWeight > 0 ? static_cast<double>(vibrantG) / vibrantWeight : 0.0,
			vibrantWeight > 0 ? static_cast<double>(vibrantB) / vibrantWeight : 0.0,
			vibrantWeight > 0
		};
	}

	Grab::Calculations::ScenePreset detectScenePresetFromStats(const RegionStats &stats)
	{
		if (stats.saturation >= 0.36 && stats.meanLuminance >= 95.0 && stats.neutralRatio <= 0.46)
			return Grab::Calculations::ScenePresetAnime;
		if (stats.luminanceContrast >= 42.0 && stats.darkRatio >= 0.24)
			return Grab::Calculations::ScenePresetGames;
		if (stats.meanLuminance <= 118.0 && stats.neutralRatio >= 0.28 && stats.brightRatio <= 0.22)
			return Grab::Calculations::ScenePresetCinema;
		return Grab::Calculations::ScenePresetNeutral;
	}

	PerceptualTuning applyScenePresetToTuning(
		PerceptualTuning tuning,
		Grab::Calculations::ScenePreset preset,
		const RegionStats &stats)
	{
		switch (preset) {
		case Grab::Calculations::ScenePresetAnime:
			tuning.vividScale += 0.18;
			tuning.vividLimit += 0.12;
			tuning.highlightKnee += 6.0;
			tuning.highlightStrength = std::max(0.14, tuning.highlightStrength - 0.05);
			tuning.warmProtection += 0.04;
			break;
		case Grab::Calculations::ScenePresetGames:
			tuning.rmsWeight += 0.05;
			tuning.meanWeight = std::max(0.60, tuning.meanWeight - 0.05);
			tuning.vividScale += 0.08;
			tuning.highlightKnee -= 8.0;
			tuning.highlightStrength += 0.03;
			break;
		case Grab::Calculations::ScenePresetCinema:
			tuning.meanWeight += 0.05;
			tuning.rmsWeight = std::max(0.08, tuning.rmsWeight - 0.05);
			tuning.vividScale = std::max(0.12, tuning.vividScale - 0.10);
			tuning.vividLimit = std::max(0.10, tuning.vividLimit - 0.08);
			tuning.highlightKnee -= 10.0;
			tuning.highlightStrength += 0.06;
			tuning.warmProtection += 0.02;
			break;
		case Grab::Calculations::ScenePresetAuto:
		case Grab::Calculations::ScenePresetNeutral:
		default:
			break;
		}

		if (stats.brightRatio > 0.35)
			tuning.highlightStrength += 0.05;
		if (stats.darkRatio > 0.45 && stats.saturation < 0.24)
			tuning.vividScale += 0.06;
		return tuning;
	}

	ColorValue finalizePerceptualColor(
		const RegionStats &stats,
		PerceptualTuning tuning,
		bool smartCalibration)
	{
		double outR = tuning.meanWeight * stats.meanR + tuning.rmsWeight * stats.rmsR;
		double outG = tuning.meanWeight * stats.meanG + tuning.rmsWeight * stats.rmsG;
		double outB = tuning.meanWeight * stats.meanB + tuning.rmsWeight * stats.rmsB;

		if (stats.hasVividColor) {
			double vividR = stats.vividR;
			double vividG = stats.vividG;
			double vividB = stats.vividB;

			const double targetLuminance = (54.0 * outR + 183.0 * outG + 19.0 * outB) / 256.0;
			const double vividLuminance = (54.0 * vividR + 183.0 * vividG + 19.0 * vividB) / 256.0;
			if (vividLuminance > 0.5) {
				const double luminanceScale = std::min(2.3, targetLuminance / vividLuminance);
				vividR *= luminanceScale;
				vividG *= luminanceScale;
				vividB *= luminanceScale;
			}

			const double vividBlend = std::min(tuning.vividLimit, stats.colorfulness * tuning.vividScale);
			outR += (vividR - outR) * vividBlend;
			outG += (vividG - outG) * vividBlend;
			outB += (vividB - outB) * vividBlend;

			// Secondary hues are the first colors an RGB mean tends to destroy:
			// magenta drifts to red and cyan drifts to green because green carries
			// most luminance. When two vivid channels clearly beat the third,
			// lock a little more strongly to that hue while retaining luminance.
			const double vividMax = std::max(vividR, std::max(vividG, vividB));
			const double vividMin = std::min(vividR, std::min(vividG, vividB));
			const double second = vividR + vividG + vividB - vividMax - vividMin;
			const double secondaryStrength = second - vividMin;
			if (vividMax - vividMin > 42.0 && secondaryStrength > 24.0 && stats.colorfulness > 0.16) {
				const double hueLock = std::min(0.34, 0.12 + secondaryStrength / 420.0);
				outR += (vividR - outR) * hueLock;
				outG += (vividG - outG) * hueLock;
				outB += (vividB - outB) * hueLock;
			}
		}

		if (smartCalibration) {
			const double floodRisk = std::min(1.0,
				stats.brightRatio * 0.58 +
				stats.neutralRatio * 0.24 +
				stats.warmBias * 0.95 +
				(stats.meanLuminance / 255.0) * 0.28);
			if (floodRisk > 0.0) {
				const double luminance = (54.0 * outR + 183.0 * outG + 19.0 * outB) / 256.0;
				const double clampFactor = 1.0 - 0.24 * floodRisk * std::min(1.0, luminance / 255.0);
				outR *= clampFactor;
				outG *= clampFactor;
				outB *= clampFactor;
			}

			if (stats.darkRatio > 0.35 && stats.meanLuminance < 92.0) {
				const double avg = (outR + outG + outB) / 3.0;
				const double separationBoost = 1.0 + std::min(0.22, 0.08 + stats.luminanceContrast / 180.0);
				outR = avg + (outR - avg) * separationBoost;
				outG = avg + (outG - avg) * separationBoost;
				outB = avg + (outB - avg) * separationBoost;
			}
		}

		const double luminance = (54.0 * outR + 183.0 * outG + 19.0 * outB) / 256.0;
		if (luminance > tuning.highlightKnee) {
			const double excess = (luminance - tuning.highlightKnee) / std::max(1.0, 255.0 - tuning.highlightKnee);
			const double clampFactor = 1.0 - tuning.highlightStrength * excess * excess;
			outR *= clampFactor;
			outG *= clampFactor;
			outB *= clampFactor;
		}

		const double maxChannel = std::max(outR, std::max(outG, outB));
		const double minChannel = std::min(outR, std::min(outG, outB));
		const double chroma = std::max(1.0, maxChannel - minChannel);
		const double warmBias = std::max(0.0, (outR + outG) * 0.5 - outB);
		const double warmWhiteFactor = std::min(1.0, warmBias / chroma / 3.0);
		if (warmWhiteFactor > 0.0) {
			const double warmClamp = 1.0 - tuning.warmProtection * warmWhiteFactor;
			outR *= warmClamp;
			outG *= warmClamp;
		}

		return ColorValue{
			static_cast<int>(std::max(0.0, std::min(255.0, outR)) + 0.5),
			static_cast<int>(std::max(0.0, std::min(255.0, outG)) + 0.5),
			static_cast<int>(std::max(0.0, std::min(255.0, outB)) + 0.5)
		};
	}

auto accumulateARGB = accumulateBuffer<PIXEL_FORMAT_ARGB>;
auto accumulateABGR = accumulateBuffer<PIXEL_FORMAT_ABGR>;
auto accumulateRGBA = accumulateBuffer<PIXEL_FORMAT_RGBA>;
auto accumulateBGRA = accumulateBuffer<PIXEL_FORMAT_BGRA>;

#if defined(__SSE4_1__) || defined(__AVX2__) || (defined(__AVX512F__) && defined(__AVX512BW__))
#ifdef __SSE4_1__
	template<uint8_t offsetR, uint8_t offsetG, uint8_t offsetB>
	static ColorValue accumulateBuffer128(
		const int * const buffer,
		const size_t pitch,
		const QRect& rect) {

		__m128i sum[bytesPerPixel] = {
			_mm_setzero_si128(),
			_mm_setzero_si128(),
			_mm_setzero_si128(),
			_mm_setzero_si128()
		};

		// masks to re-arrange ARGB into 000A, 000R...
		// without doing right shift (ARGB >> 2*8 => 00AR) and applying AND mask (00AR & 000F => 000R)
		// to isolate color components
		constexpr const char zero = (char)(1<<7);
		const __m128i shuffleR = _mm_set_epi8(
			zero,zero,zero,3*4+offsetR,
			zero,zero,zero,2*4+offsetR,
			zero,zero,zero,1*4+offsetR,
			zero,zero,zero,0*4+offsetR
		);
		const __m128i shuffleG = _mm_set_epi8(
			zero,zero,zero,3*4+offsetG,
			zero,zero,zero,2*4+offsetG,
			zero,zero,zero,1*4+offsetG,
			zero,zero,zero,0*4+offsetG
		);
		const __m128i shuffleB = _mm_set_epi8(
			zero,zero,zero,3*4+offsetB,
			zero,zero,zero,2*4+offsetB,
			zero,zero,zero,1*4+offsetB,
			zero,zero,zero,0*4+offsetB
		);
		const size_t softlimit = rect.width() / pixelsPerStep;
		if (softlimit > 0)
			for (size_t currentY = 0; currentY < (size_t)rect.height(); ++currentY) {
				for (size_t currentX = 0; currentX < softlimit; ++currentX) {
					const size_t index = pitch * (rect.y() + currentY) + rect.x() + currentX * pixelsPerStep;
					// (AARRGGBB AARRGGBB AARRGGBB AARRGGBB)
					const __m128i vec4 = _mm_loadu_si128((const __m128i*)&buffer[index]);

					//   (AARRGGBB AARRGGBB AARRGGBB AARRGGBB) shuffleR
					// = (000000RR 000000RR 000000RR 000000RR)
					sum[offsetR] = _mm_add_epi32(sum[offsetR], _mm_shuffle_epi8(vec4, shuffleR));
					sum[offsetG] = _mm_add_epi32(sum[offsetG], _mm_shuffle_epi8(vec4, shuffleG));
					sum[offsetB] = _mm_add_epi32(sum[offsetB], _mm_shuffle_epi8(vec4, shuffleB));
				}
			}
		//   ((BBBBBBBB BBBBBBBB BBBBBBBB BBBBBBBB) + (GGGGGGGG GGGGGGGG GGGGGGGG GGGGGGGG))
		// + ((RRRRRRRR RRRRRRRR RRRRRRRR RRRRRRRR) + (AAAAAAAA AAAAAAAA AAAAAAAA AAAAAAAA))
		// = ((GGGGGGGG GGGGGGGG BBBBBBBB BBBBBBBB) + (AAAAAAAA AAAAAAAA RRRRRRRR RRRRRRRR))
		// =  (AAAAAAAA RRRRRRRR GGGGGGGG BBBBBBBB)
		const __m128i horizontalSum128 = _mm_hadd_epi32(_mm_hadd_epi32(sum[0], sum[1]), _mm_hadd_epi32(sum[2], sum[3]));
		const size_t count = rect.height() * rect.width();

		ColorValue color{0,0,0};

		const int delta = rect.width() % pixelsPerStep;
		for (int currentX = rect.width() - delta; currentX < rect.width(); ++currentX) {
			for (int currentY = 0; currentY < rect.height(); ++currentY) {
				const size_t index = pitch * (rect.y() + currentY) + (rect.x() + currentX);
				color.r += ((const unsigned char* const)&buffer[index])[offsetR];
				color.g += ((const unsigned char* const)&buffer[index])[offsetG];
				color.b += ((const unsigned char* const)&buffer[index])[offsetB];
			}
		}
		color.r = ((color.r + _mm_extract_epi32(horizontalSum128, offsetR)) / count) & 0xff;
		color.g = ((color.g + _mm_extract_epi32(horizontalSum128, offsetG)) / count) & 0xff;
		color.b = ((color.b + _mm_extract_epi32(horizontalSum128, offsetB)) / count) & 0xff;
		return color;
	};
#endif // ifdef __SSE4_1__

#ifdef __AVX2__
	template<uint8_t offsetR, uint8_t offsetG, uint8_t offsetB>
	static ColorValue accumulateBuffer256(
		const int * const buffer,
		const size_t pitch,
		const QRect& rect) {

		__m256i sum[bytesPerPixel] = {
			_mm256_setzero_si256(),
			_mm256_setzero_si256(),
			_mm256_setzero_si256(),
			_mm256_setzero_si256()
		}; // A,R,G,B sums

		constexpr const char zero = (char)(1<<7);
		const __m256i shuffleR = _mm256_broadcastsi128_si256(_mm_set_epi8(
			zero,zero,zero,3*4+offsetR,
			zero,zero,zero,2*4+offsetR,
			zero,zero,zero,1*4+offsetR,
			zero,zero,zero,0*4+offsetR
		));
		const __m256i shuffleG = _mm256_broadcastsi128_si256(_mm_set_epi8(
			zero,zero,zero,3*4+offsetG,
			zero,zero,zero,2*4+offsetG,
			zero,zero,zero,1*4+offsetG,
			zero,zero,zero,0*4+offsetG
		));
		const __m256i shuffleB = _mm256_broadcastsi128_si256(_mm_set_epi8(
			zero,zero,zero,3*4+offsetB,
			zero,zero,zero,2*4+offsetB,
			zero,zero,zero,1*4+offsetB,
			zero,zero,zero,0*4+offsetB
		));
		// 2 part processing:
		//   1) inner rect with multiple-of-8 width so we can do full 8px loads all the way
		const size_t softlimit = rect.width() / pixelsPerStep / 2;
		if (softlimit > 0)
			for (size_t currentY = 0; currentY < (size_t)rect.height(); ++currentY) {
				for (size_t currentX = 0; currentX < softlimit; ++currentX) {
					const size_t index = pitch * (rect.y() + currentY) + rect.x() + currentX * pixelsPerStep * 2;
					const __m256i vec8 = _mm256_loadu_si256((const __m256i*)&buffer[index]);
					sum[offsetR] = _mm256_add_epi32(sum[offsetR], _mm256_shuffle_epi8(vec8, shuffleR));
					sum[offsetG] = _mm256_add_epi32(sum[offsetG], _mm256_shuffle_epi8(vec8, shuffleG));
					sum[offsetB] = _mm256_add_epi32(sum[offsetB], _mm256_shuffle_epi8(vec8, shuffleB));
				}
			}
		//   2) the remaining delta-px wide rect
		const size_t delta = rect.width() % (pixelsPerStep * 2);
		if (delta > 0) {
			// masks to load only delta number of pixels
			const __m256i loadmasks[7] = {
				_mm256_setr_epi32(0xFFFFFFFFU,0,0,0,0,0,0,0),
				_mm256_setr_epi32(0xFFFFFFFFU,0xFFFFFFFFU,0,0,0,0,0,0),
				_mm256_setr_epi32(0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0,0,0,0,0),
				_mm256_setr_epi32(0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0,0,0,0),
				_mm256_setr_epi32(0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0,0,0),
				_mm256_setr_epi32(0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0,0),
				_mm256_setr_epi32(0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU,0)
			};
			const __m256i loadmask = loadmasks[delta - 1];
			for (size_t currentY = 0; currentY < (size_t)rect.height(); ++currentY) {
				const size_t index = pitch * (rect.y() + currentY) + rect.x() + softlimit * pixelsPerStep * 2;
				const __m256i vec8 = _mm256_maskload_epi32(&buffer[index], loadmask);
				sum[offsetR] = _mm256_add_epi32(sum[offsetR], _mm256_shuffle_epi8(vec8, shuffleR));
				sum[offsetG] = _mm256_add_epi32(sum[offsetG], _mm256_shuffle_epi8(vec8, shuffleG));
				sum[offsetB] = _mm256_add_epi32(sum[offsetB], _mm256_shuffle_epi8(vec8, shuffleB));
			}
		}

		const __m256i horizontalSum256 = _mm256_hadd_epi32(_mm256_hadd_epi32(sum[0], sum[1]) , _mm256_hadd_epi32(sum[2], sum[3]));
		const __m128i horizontalSum128 = _mm_add_epi32(_mm256_extracti128_si256(horizontalSum256, 0), _mm256_extracti128_si256(horizontalSum256, 1));
		const size_t count = rect.height() * rect.width();
		ColorValue color;
		color.r = (_mm_extract_epi32(horizontalSum128, offsetR) / count) & 0xff;
		color.g = (_mm_extract_epi32(horizontalSum128, offsetG) / count) & 0xff;
		color.b = (_mm_extract_epi32(horizontalSum128, offsetB) / count) & 0xff;
		return color;
	};
#endif // ifdef __AVX2__
#if defined(__AVX512F__) && defined(__AVX512BW__)
	template<uint8_t offsetR, uint8_t offsetG, uint8_t offsetB>
	static ColorValue accumulateBuffer512(
		const int * const buffer,
		const size_t pitch,
		const QRect& rect) {

		__m512i sum[bytesPerPixel] = {
			_mm512_setzero_epi32(),
			_mm512_setzero_epi32(),
			_mm512_setzero_epi32(),
			_mm512_setzero_epi32()
		}; // A,R,G,B sums

		constexpr const uint32_t zero = (1 << 7);

		const __m512i shuffleR = _mm512_set4_epi32(
			(zero << 24) | (zero << 16) | (zero << 8) | (3*4+offsetR),
			(zero << 24) | (zero << 16) | (zero << 8) | (2*4+offsetR),
			(zero << 24) | (zero << 16) | (zero << 8) | (1*4+offsetR),
			(zero << 24) | (zero << 16) | (zero << 8) | (0*4+offsetR)
		);
		const __m512i shuffleG = _mm512_set4_epi32(
			(zero << 24) | (zero << 16) | (zero << 8) | (3*4+offsetG),
			(zero << 24) | (zero << 16) | (zero << 8) | (2*4+offsetG),
			(zero << 24) | (zero << 16) | (zero << 8) | (1*4+offsetG),
			(zero << 24) | (zero << 16) | (zero << 8) | (0*4+offsetG)
		);
		const __m512i shuffleB = _mm512_set4_epi32(
			(zero << 24) | (zero << 16) | (zero << 8) | (3*4+offsetB),
			(zero << 24) | (zero << 16) | (zero << 8) | (2*4+offsetB),
			(zero << 24) | (zero << 16) | (zero << 8) | (1*4+offsetB),
			(zero << 24) | (zero << 16) | (zero << 8) | (0*4+offsetB)
		);

		constexpr const int stepsPerLoad = 4;
		constexpr const int pixelsPerLoad = pixelsPerStep * stepsPerLoad;
		const size_t softlimit = rect.width() / pixelsPerLoad;
		const size_t delta = (size_t)rect.width() - (softlimit * pixelsPerLoad);
		const __mmask16 deltamask = 0xFFFF >> (16 - delta);
		for (size_t currentY = 0; currentY < (size_t)rect.height(); ++currentY) {
			for (size_t currentX = 0; currentX <= softlimit; ++currentX) {
				const size_t index = pitch * (rect.y() + currentY) + rect.x() + currentX * pixelsPerLoad; // starting offset for lines
				const __m512i vec8 = _mm512_maskz_loadu_epi32(currentX == softlimit ? deltamask : __mmask16(0xFFFF), &buffer[index]);
				sum[offsetR] = _mm512_add_epi32(sum[offsetR], _mm512_shuffle_epi8(vec8, shuffleR));
				sum[offsetG] = _mm512_add_epi32(sum[offsetG], _mm512_shuffle_epi8(vec8, shuffleG));
				sum[offsetB] = _mm512_add_epi32(sum[offsetB], _mm512_shuffle_epi8(vec8, shuffleB));
			}
		}
		const size_t count = rect.height() * rect.width();
		ColorValue color;
		color.r = (_mm512_reduce_add_epi32(sum[offsetR]) / count) & 0xff;
		color.g = (_mm512_reduce_add_epi32(sum[offsetG]) / count) & 0xff;
		color.b = (_mm512_reduce_add_epi32(sum[offsetB]) / count) & 0xff;
		return color;
	};
#endif // ifdef __AVX512F__ && __AVX512BW__

enum SIMDLevel {
	None = 0,
	SSE4_1 = 1 << 0,
	AVX2 = 1 << 1,
	AVX512 = 1 << 2
};

#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
// https://developer.apple.com/documentation/apple_silicon/about_the_rosetta_translation_environment?language=objc
static uint32_t available_simd() {
	uint32_t level = SIMDLevel::None;
	int ret = 0;
	size_t size = sizeof(ret);
	if (sysctlbyname("hw.optional.avx2_0", &ret, &size, NULL, 0) == 0 && ret == 1)
		level |= SIMDLevel::AVX2;
	ret = 0;
	size = sizeof(ret);
	if (sysctlbyname("hw.optional.sse4_1", &ret, &size, NULL, 0) == 0 && ret == 1)
		level |= SIMDLevel::SSE4_1;
	return level;
}
#elif defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 1300)
// https://software.intel.com/en-us/articles/how-to-detect-new-instruction-support-in-the-4th-generation-intel-core-processor-family
static uint32_t available_simd() {
	uint32_t level = SIMDLevel::None;
	if (_may_i_use_cpu_feature(_FEATURE_AVX512F | _FEATURE_AVX512BW))
		level |= SIMDLevel::AVX512;
	if (_may_i_use_cpu_feature(_FEATURE_AVX2))
		level |= SIMDLevel::AVX2;
	if (_may_i_use_cpu_feature(_FEATURE_SSE4_1))
		level |= SIMDLevel::SSE4_1;
	return level;
}
#else /* non-Intel compiler */
void run_cpuid(uint32_t eax, uint32_t ecx, uint32_t* abcd)
{
#if defined(_MSC_VER)
	__cpuidex((int*)abcd, eax, ecx);
#else
	uint32_t ebx = 0;
	uint32_t edx = 0;
# if defined( __i386__ ) && defined ( __PIC__ )
	 /* in case of PIC under 32-bit EBX cannot be clobbered */
	__asm__ ( "movl %%ebx, %%edi \n\t cpuid \n\t xchgl %%ebx, %%edi" : "=D" (ebx),
# else
	__asm__ ( "cpuid" : "+b" (ebx),
# endif // ifdef __i386__ || __PIC__
			  "+a" (eax), "+c" (ecx), "=d" (edx) );
	abcd[0] = eax; abcd[1] = ebx; abcd[2] = ecx; abcd[3] = edx;
#endif // ifdef _MSC_VER
}


#if defined(_MSC_VER)
# include <intrin.h>
#endif // ifdef _MSC_VER
static uint32_t available_simd() {
	uint32_t abcd[4] = {0,0,0,0};

	run_cpuid(1, 0, abcd);
	uint32_t eax = 0x07;
	uint32_t ecx = 0x00;
#if defined(_MSC_VER)
	__cpuidex((int*)abcd, eax, ecx);
#else
	uint32_t ebx = 0;
	uint32_t edx = 0;
# if defined( __i386__ ) && defined ( __PIC__ )
	 /* in case of PIC under 32-bit EBX cannot be clobbered */
	__asm__ ( "movl %%ebx, %%edi \n\t cpuid \n\t xchgl %%ebx, %%edi" : "=D" (ebx),
# else
	__asm__ ( "cpuid" : "+b" (ebx),
# endif // ifdef __i386__ || __PIC__
			  "+a" (eax), "+c" (ecx), "=d" (edx) );
	abcd[0] = eax; abcd[1] = ebx; abcd[2] = ecx; abcd[3] = edx;
#endif // ifdef _MSC_VER
	uint32_t level = SIMDLevel::None;
	run_cpuid(7, 0, abcd);
	// CPUID.(EAX=07H, ECX=0H):EBX.AVX512F [bit 16]==1
	// CPUID.(EAX=07H, ECX=0H):EBX.AVX512BW[bit 30]==1
	if ((abcd[1] & (1 << 16)) && (abcd[1] & (1 << 30)))
		level |= SIMDLevel::AVX512;

	// CPUID.(EAX=07H, ECX=0H):EBX.AVX2[bit 5]==1
	if ((abcd[1] & (1 << 5)))
		level |= SIMDLevel::AVX2;

	// CPUID.(EAX=01H, ECX=0H):ECX.SSE4_1[bit 19]==1
	run_cpuid(1, 0, abcd);
	if ((abcd[2] & (1 << 19)))
		level |= SIMDLevel::SSE4_1;

	return level;
}
#endif // else non-intel

/*
	accumulateBuffer128 requires SSE4.1
	accumulateBuffer256 requires AVX2

	instruction availability:
	Steam Hardware & Software Survey (March 2020)
	SSE4.1   97.88% / +0.69%
	AVX2     74.19% / +2.73%

	(September 2023)
	SSE4.1   99.55% / +0.08%
	AVX2     92.04% / +0.80%
	AVX512F  10.00% / -0.15%

	by default set functions to non-SIMD and upgrade to AVX2/512 or SSE4.1 when available
*/
struct simdupgrade {
	simdupgrade() {
		const uint32_t level = available_simd();
		#ifdef __SSE4_1__
		if (level & SIMDLevel::SSE4_1) {
			accumulateARGB = accumulateBuffer128<PIXEL_FORMAT_ARGB>;
			accumulateABGR = accumulateBuffer128<PIXEL_FORMAT_ABGR>;
			accumulateRGBA = accumulateBuffer128<PIXEL_FORMAT_RGBA>;
			accumulateBGRA = accumulateBuffer128<PIXEL_FORMAT_BGRA>;
		}
		#endif // ifdef __SSE4_1__
		#ifdef __AVX2__
		if (level & SIMDLevel::AVX2) {
			accumulateARGB = accumulateBuffer256<PIXEL_FORMAT_ARGB>;
			accumulateABGR = accumulateBuffer256<PIXEL_FORMAT_ABGR>;
			accumulateRGBA = accumulateBuffer256<PIXEL_FORMAT_RGBA>;
			accumulateBGRA = accumulateBuffer256<PIXEL_FORMAT_BGRA>;
		}
		#endif // ifdef __AVX2__
		#if defined(__AVX512F__) && defined(__AVX512BW__)
		if (level & SIMDLevel::AVX512) {
			accumulateARGB = accumulateBuffer512<PIXEL_FORMAT_ARGB>;
			accumulateABGR = accumulateBuffer512<PIXEL_FORMAT_ABGR>;
			accumulateRGBA = accumulateBuffer512<PIXEL_FORMAT_RGBA>;
			accumulateBGRA = accumulateBuffer512<PIXEL_FORMAT_BGRA>;
		}
		#endif // ifdef __AVX512F__ && __AVX512BW__
	}
};
simdupgrade avxup;
#endif // ifdef __SSE4_1__ || __AVX2__ || (__AVX512F__ && __AVX512BW__)
} // namespace

namespace Grab {
	namespace Calculations {
		namespace {
			PerceptualTuning tuningForMode(ColorProcessingMode mode)
			{
				switch (mode) {
				case ColorProcessingModeLegacy:
					return {1.0, 0.0, 0.0, 0.0, 255.0, 0.0, 0.0};
				case ColorProcessingModeAccurate:
					return {0.88, 0.12, 0.26, 0.18, 172.0, 0.32, 0.10};
				case ColorProcessingModeCinema:
					return {0.74, 0.26, 0.52, 0.34, 164.0, 0.24, 0.18};
				case ColorProcessingModeBalanced:
				default:
					return {0.82, 0.18, 0.40, 0.25, 168.0, 0.28, 0.14};
				}
			}
		}

		QRgb calculateAvgColor(const unsigned char * const buffer, BufferFormat bufferFormat, const size_t pitch, const QRect &rect) {

			ColorValue color;
			switch(bufferFormat) {
			case BufferFormatArgb:
				color = accumulateARGB((int*)buffer, pitch / bytesPerPixel, rect);
				break;

			case BufferFormatAbgr:
				color = accumulateABGR((int*)buffer, pitch / bytesPerPixel, rect);
				break;

			case BufferFormatRgba:
				color = accumulateRGBA((int*)buffer, pitch / bytesPerPixel, rect);
				break;

			case BufferFormatBgra:
				color = accumulateBGRA((int*)buffer, pitch / bytesPerPixel, rect);
				break;
			default:
				return -1;
				break;
			}

			return qRgb(color.r, color.g, color.b);
		}

		QRgb calculatePerceptualColor(const unsigned char * const buffer, BufferFormat bufferFormat, const size_t pitch, const QRect &rect) {
			return calculateColor(buffer, bufferFormat, pitch, rect, ColorProcessingModeBalanced);
		}

		QRgb calculateColor(const unsigned char * const buffer, BufferFormat bufferFormat, const size_t pitch, const QRect &rect, ColorProcessingMode mode) {
			ProcessingOptions options;
			options.colorMode = mode;
			options.scenePreset = ScenePresetNeutral;
			options.smartCalibration = true;
			return calculateColor(buffer, bufferFormat, pitch, rect, options);
		}

		ScenePreset detectScenePreset(const unsigned char * const buffer, BufferFormat bufferFormat, const size_t pitch, const QRect &rect)
		{
			switch (bufferFormat) {
			case BufferFormatArgb:
				return detectScenePresetFromStats(analyzeRegion<PIXEL_FORMAT_ARGB>(buffer, pitch, rect));
			case BufferFormatAbgr:
				return detectScenePresetFromStats(analyzeRegion<PIXEL_FORMAT_ABGR>(buffer, pitch, rect));
			case BufferFormatRgba:
				return detectScenePresetFromStats(analyzeRegion<PIXEL_FORMAT_RGBA>(buffer, pitch, rect));
			case BufferFormatBgra:
				return detectScenePresetFromStats(analyzeRegion<PIXEL_FORMAT_BGRA>(buffer, pitch, rect));
			default:
				return ScenePresetNeutral;
			}
		}

		QRgb calculateColor(const unsigned char * const buffer, BufferFormat bufferFormat, const size_t pitch, const QRect &rect, const ProcessingOptions &options) {
			ColorValue color;
			PerceptualTuning tuning = tuningForMode(options.colorMode);
			switch(bufferFormat) {
			case BufferFormatArgb:
				if (options.colorMode == ColorProcessingModeLegacy) {
					color = accumulateARGB((int*)buffer, pitch / bytesPerPixel, rect);
				} else {
					const RegionStats stats = analyzeRegion<PIXEL_FORMAT_ARGB>(buffer, pitch, rect);
					tuning = applyScenePresetToTuning(tuning, options.scenePreset, stats);
					color = finalizePerceptualColor(stats, tuning, options.smartCalibration);
				}
				break;
			case BufferFormatAbgr:
				if (options.colorMode == ColorProcessingModeLegacy) {
					color = accumulateABGR((int*)buffer, pitch / bytesPerPixel, rect);
				} else {
					const RegionStats stats = analyzeRegion<PIXEL_FORMAT_ABGR>(buffer, pitch, rect);
					tuning = applyScenePresetToTuning(tuning, options.scenePreset, stats);
					color = finalizePerceptualColor(stats, tuning, options.smartCalibration);
				}
				break;
			case BufferFormatRgba:
				if (options.colorMode == ColorProcessingModeLegacy) {
					color = accumulateRGBA((int*)buffer, pitch / bytesPerPixel, rect);
				} else {
					const RegionStats stats = analyzeRegion<PIXEL_FORMAT_RGBA>(buffer, pitch, rect);
					tuning = applyScenePresetToTuning(tuning, options.scenePreset, stats);
					color = finalizePerceptualColor(stats, tuning, options.smartCalibration);
				}
				break;
			case BufferFormatBgra:
				if (options.colorMode == ColorProcessingModeLegacy) {
					color = accumulateBGRA((int*)buffer, pitch / bytesPerPixel, rect);
				} else {
					const RegionStats stats = analyzeRegion<PIXEL_FORMAT_BGRA>(buffer, pitch, rect);
					tuning = applyScenePresetToTuning(tuning, options.scenePreset, stats);
					color = finalizePerceptualColor(stats, tuning, options.smartCalibration);
				}
				break;
			default:
				return -1;
			}

			return qRgb(color.r, color.g, color.b);
		}
	}
}
