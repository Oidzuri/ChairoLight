#include "GrabCalculationTest.hpp"

void GrabCalculationTest::testCase1()
{
	unsigned char buf[16];
	memset(buf, 0xfa, 16);
	QRgb result = Grab::Calculations::calculateAvgColor(buf, BufferFormatArgb, 16, QRect(0,0,4,1));
	QVERIFY2(result == QColor(0xfa, 0xfa, 0xfa).rgb(), qPrintable(QString("Failure. calculateAvgColor returned wrong errorcode %1").arg(result, 1, 16)));
}

void GrabCalculationTest::perceptualGrayStaysNeutral()
{
	unsigned char buf[16];
	memset(buf, 128, sizeof(buf));
	const QRgb result = Grab::Calculations::calculatePerceptualColor(
		buf, BufferFormatArgb, 16, QRect(0, 0, 4, 1));
	// Tone mapping may lower mid-gray luminance, but it must not introduce a
	// color cast. Neutral input must remain channel-neutral.
	QCOMPARE(qRed(result), qGreen(result));
	QCOMPARE(qGreen(result), qBlue(result));
	QVERIFY(qRed(result) >= 115 && qRed(result) <= 128);
}

void GrabCalculationTest::perceptualColorKeepsSmallHighlights()
{
	// ARGB layout here is byte-ordered B, G, R, A. One red pixel and three
	// black pixels model a small bright object near the edge of a dark frame.
	unsigned char buf[16] = {0};
	for (int i = 0; i < 4; ++i)
		buf[i * 4 + 3] = 255;
	buf[2] = 255;

	const QRgb average = Grab::Calculations::calculateAvgColor(
		buf, BufferFormatArgb, 16, QRect(0, 0, 4, 1));
	const QRgb perceptual = Grab::Calculations::calculatePerceptualColor(
		buf, BufferFormatArgb, 16, QRect(0, 0, 4, 1));

	QVERIFY(qRed(perceptual) > qRed(average));
	QCOMPARE(qGreen(perceptual), 0);
	QCOMPARE(qBlue(perceptual), 0);
}

void GrabCalculationTest::dxgiBgraMemoryDoesNotTreatAlphaAsBlue()
{
	// DXGI B8G8R8A8 is laid out as B, G, R, A in little-endian memory.
	// The alpha byte must never be interpreted as the blue channel.
	unsigned char blackBgra[16] = {
		0, 0, 0, 255, 0, 0, 0, 255,
		0, 0, 0, 255, 0, 0, 0, 255
	};
	const QRgb result = Grab::Calculations::calculateAvgColor(
		blackBgra, BufferFormatArgb, 16, QRect(0, 0, 4, 1));
	QCOMPARE(qRed(result), 0);
	QCOMPARE(qGreen(result), 0);
	QCOMPARE(qBlue(result), 0);
}
