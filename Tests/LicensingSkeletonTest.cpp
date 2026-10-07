#include <JuceHeader.h>

#include "Entitlement.h"
#include "PackVerifier.h"
#include "ShareablePack.h"

#include <cstdlib>
#include <iostream>

namespace
{
bool check (bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

class AcceptingVerifier final : public neumaton::sharing::SignatureVerifier
{
public:
    bool verify (const juce::String& keyId,
                 const juce::String& message,
                 const juce::String& signature) const override
    {
        return keyId == "packs-test-1"
            && signature == "test-signature"
            && message.contains ("commercial");
    }
};

neumaton::sharing::PackDocument makePack()
{
    using namespace neumaton::sharing;

    PackDocument pack;
    pack.manifest.stableId = "community.pack.test";
    pack.manifest.name = "Test Pack";
    pack.manifest.author = "Tester";
    pack.manifest.creatorId = "creator.test";
    pack.manifest.license = PackLicense::community;

    juce::ValueTree scale ("Scale");
    scale.setProperty ("stableId", "scale.test", nullptr);
    scale.setProperty ("name", "Test Scale", nullptr);
    scale.setProperty ("ratios", "1,1.059463094,1.122462048", nullptr);
    pack.scales.addChild (scale, -1, nullptr);

    juce::ValueTree preset ("Preset");
    preset.setProperty ("stableId", "preset.test", nullptr);
    preset.setProperty ("name", "Test Preset", nullptr);
    pack.presets.addChild (preset, -1, nullptr);

    pack.sealIntegrity();
    return pack;
}
}

int main()
{
    using namespace neumaton::licensing;
    using namespace neumaton::sharing;

    bool ok = true;

    ok &= check (buildDefaultTier() == Tier::beta,
                 "closed_beta_build_defaults_to_beta");

    const EntitlementManager freeEntitlement (Tier::free);
    const EntitlementManager supporterEntitlement (Tier::supporter);
    const EntitlementManager studioEntitlement (Tier::supporterStudio);
    const EntitlementManager betaEntitlement (Tier::beta);

    ok &= check (freeEntitlement.canExportPack (PackLicense::community)
                 && ! freeEntitlement.canExportPack (PackLicense::commercial),
                 "free_can_export_community_not_commercial");

    ok &= check (supporterEntitlement.canExportPack (PackLicense::commercial)
                 && supporterEntitlement.deviceLimit() == 5,
                 "supporter_has_creator_right_and_five_device_policy");

    ok &= check (studioEntitlement.canExportPack (PackLicense::commercial)
                 && studioEntitlement.deviceLimit() == 15,
                 "studio_has_creator_right_and_fifteen_device_policy");

    ok &= check (betaEntitlement.canUseSupporterContent()
                 && betaEntitlement.hasEarlyAccess()
                 && ! betaEntitlement.canExportPack (PackLicense::commercial),
                 "beta_is_not_a_paid_creator_licence");

    auto pack = makePack();
    ok &= check (pack.verifyIntegrity() == PackIntegrityStatus::valid,
                 "schema3_pack_hash_valid");

    const auto firstHash = pack.manifest.payloadSha256;

    auto reordered = makePack();
    juce::ValueTree secondScale ("Scale");
    secondScale.setProperty ("stableId", "scale.second", nullptr);
    secondScale.setProperty ("name", "Second", nullptr);
    reordered.scales.addChild (secondScale, 0, nullptr);
    reordered.sealIntegrity();

    auto sameContentDifferentOrder = makePack();
    sameContentDifferentOrder.scales.addChild (secondScale.createCopy(), -1, nullptr);
    sameContentDifferentOrder.sealIntegrity();

    ok &= check (reordered.manifest.payloadSha256
                     == sameContentDifferentOrder.manifest.payloadSha256,
                 "payload_hash_is_order_independent");

    pack.scales.getChild (0).setProperty ("name", "Tampered", nullptr);
    ok &= check (pack.verifyIntegrity() == PackIntegrityStatus::hashMismatch,
                 "payload_mutation_is_detected");

    pack.sealIntegrity();
    ok &= check (pack.verifyIntegrity() == PackIntegrityStatus::valid
                 && pack.manifest.payloadSha256 != firstHash,
                 "reseal_updates_payload_hash");

    const auto communitySigningPayload = pack.signingPayload();
    pack.manifest.license = PackLicense::commercial;
    const auto commercialSigningPayload = pack.signingPayload();
    ok &= check (communitySigningPayload != commercialSigningPayload,
                 "license_class_is_covered_by_signing_payload");

    auto unsignedCommercial = verifyPack (pack);
    ok &= check (unsignedCommercial.contentUsable()
                 && ! unsignedCommercial.commercialClaimVerified()
                 && unsignedCommercial.signature == PackSignatureStatus::unsignedPack,
                 "unsigned_commercial_claim_is_not_verified");

    pack.manifest.keyId = "packs-test-1";
    pack.manifest.signature = "test-signature";
    AcceptingVerifier acceptingVerifier;
    const auto signedCommercial = verifyPack (pack, &acceptingVerifier);
    ok &= check (signedCommercial.commercialClaimVerified(),
                 "signature_verifier_boundary_can_authorise_commercial_claim");

    juce::ValueTree legacyRoot ("ErgasterionCommunityPack");
    legacyRoot.setProperty ("schemaVersion", 2, nullptr);
    juce::ValueTree legacyManifest ("Manifest");
    legacyManifest.setProperty ("schemaVersion", 2, nullptr);
    legacyManifest.setProperty ("stableId", "community.pack.legacy", nullptr);
    legacyManifest.setProperty ("name", "Legacy", nullptr);
    legacyManifest.setProperty ("author", "Legacy Author", nullptr);
    legacyRoot.addChild (legacyManifest, -1, nullptr);
    legacyRoot.addChild (juce::ValueTree ("Scales"), -1, nullptr);
    legacyRoot.addChild (juce::ValueTree ("Presets"), -1, nullptr);
    legacyRoot.addChild (juce::ValueTree ("Scenes"), -1, nullptr);

    const auto legacy = PackDocument::fromValueTree (legacyRoot);
    ok &= check (legacy.isValid()
                 && legacy.manifest.license == PackLicense::community
                 && legacy.verifyIntegrity() == PackIntegrityStatus::legacyUnsigned,
                 "schema2_is_backward_compatible_and_community_only");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
