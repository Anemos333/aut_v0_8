#pragma once

#include <JuceHeader.h>

#include "ShareablePack.h"

namespace neumaton::sharing
{
enum class PackSignatureStatus
{
    unsignedPack,
    malformed,
    verifierUnavailable,
    valid,
    invalid
};

class SignatureVerifier
{
public:
    virtual ~SignatureVerifier() = default;

    // The concrete implementation will be supplied when the Neumaton server
    // exists. The plugin side only needs public verification material.
    [[nodiscard]] virtual bool verify (const juce::String& keyId,
                                       const juce::String& message,
                                       const juce::String& signature) const = 0;
};

struct PackVerification
{
    PackIntegrityStatus integrity = PackIntegrityStatus::missingHash;
    PackSignatureStatus signature = PackSignatureStatus::unsignedPack;
    PackLicense claimedLicense = PackLicense::community;

    [[nodiscard]] bool contentUsable() const noexcept
    {
        return integrity == PackIntegrityStatus::valid
            || integrity == PackIntegrityStatus::legacyUnsigned;
    }

    [[nodiscard]] bool commercialClaimVerified() const noexcept
    {
        return claimedLicense == PackLicense::commercial
            && integrity == PackIntegrityStatus::valid
            && signature == PackSignatureStatus::valid;
    }
};

[[nodiscard]] inline PackVerification verifyPack (
    const PackDocument& document,
    const SignatureVerifier* verifier = nullptr)
{
    PackVerification result;
    result.integrity = document.verifyIntegrity();
    result.claimedLicense = document.manifest.license;

    if (document.manifest.hasPartialSignature())
    {
        result.signature = PackSignatureStatus::malformed;
        return result;
    }

    if (! document.manifest.hasCompleteSignature())
    {
        result.signature = PackSignatureStatus::unsignedPack;
        return result;
    }

    if (verifier == nullptr)
    {
        result.signature = PackSignatureStatus::verifierUnavailable;
        return result;
    }

    result.signature = verifier->verify (
        document.manifest.keyId,
        document.signingPayload(),
        document.manifest.signature)
            ? PackSignatureStatus::valid
            : PackSignatureStatus::invalid;

    return result;
}
} // namespace neumaton::sharing
