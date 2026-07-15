# Windows code signing

ChairoLight can be built without a certificate, but unsigned downloads can trigger Microsoft SmartScreen warnings. A trusted publisher name requires an Authenticode code-signing certificate issued to the release owner.

## Recommended release setup

1. Obtain an OV or EV code-signing certificate from a trusted certificate authority.
2. Store certificate material outside the repository. Prefer a hardware-backed or cloud signing service over an exportable PFX.
3. If a PFX must be used in GitHub Actions, store its Base64 value and password as repository/environment secrets, restrict the release environment and never expose them to pull-request workflows.
4. Sign `Prismatik.exe` before creating the ZIP.
5. Use SHA-256 file and timestamp digests with the timestamp server required by the certificate provider.
6. Verify the signature and publisher before uploading the archive.

Example verification on a trusted Windows machine:

```powershell
Get-AuthenticodeSignature .\Prismatik.exe | Format-List Status,StatusMessage,SignerCertificate
```

The CI workflow intentionally does not contain a fake or self-signed release certificate. Its optional signing step only runs when release secrets are configured.
