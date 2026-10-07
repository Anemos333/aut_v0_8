#pragma once

namespace neumaton::licensing
{
enum class Tier
{
    free,
    supporter,
    supporterStudio,
    beta
};

enum class PackLicense
{
    community,
    commercial
};

struct LicensePolicy
{
    bool supporterContent = false;
    bool commercialPackExport = false;
    bool earlyAccess = false;

    // Zero means that the local plugin does not enforce a device count for this
    // tier. Paid-tier device limits are server-side policy, never audio policy.
    int deviceLimit = 0;
};

[[nodiscard]] constexpr LicensePolicy policyFor (Tier tier) noexcept
{
    switch (tier)
    {
        case Tier::supporter:
            return { true, true, true, 5 };

        case Tier::supporterStudio:
            return { true, true, true, 15 };

        case Tier::beta:
            // Closed beta exposes supporter-facing content for validation, but
            // it is not itself a paid creator licence.
            return { true, false, true, 0 };

        case Tier::free:
        default:
            return {};
    }
}

[[nodiscard]] constexpr Tier buildDefaultTier() noexcept
{
   #if defined (NEUMATON_CLOSED_BETA) && NEUMATON_CLOSED_BETA
    return Tier::beta;
   #else
    return Tier::free;
   #endif
}

class EntitlementManager
{
public:
    EntitlementManager() noexcept = default;
    explicit EntitlementManager (Tier tier) noexcept : tier_ (tier) {}

    [[nodiscard]] Tier getTier() const noexcept
    {
        return tier_;
    }

    [[nodiscard]] LicensePolicy getPolicy() const noexcept
    {
        return policyFor (tier_);
    }

    [[nodiscard]] bool canUseSupporterContent() const noexcept
    {
        return getPolicy().supporterContent;
    }

    [[nodiscard]] bool hasEarlyAccess() const noexcept
    {
        return getPolicy().earlyAccess;
    }

    [[nodiscard]] bool canExportPack (PackLicense license) const noexcept
    {
        if (license == PackLicense::community)
            return true;

        return getPolicy().commercialPackExport;
    }

    [[nodiscard]] int deviceLimit() const noexcept
    {
        return getPolicy().deviceLimit;
    }

    // The future signature/network layer must validate an entitlement before
    // calling this. Keeping that boundary explicit prevents licensing code from
    // leaking into the audio engine while the server does not yet exist.
    void acceptVerifiedTier (Tier tier) noexcept
    {
        tier_ = tier;
    }

    void clearToFree() noexcept
    {
        tier_ = Tier::free;
    }

private:
    Tier tier_ = buildDefaultTier();
};
} // namespace neumaton::licensing
