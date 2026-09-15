from pathlib import Path

script_path = Path(__file__).resolve().parent / 'RefineVoiceAwarePathRolesV1.py'
source = script_path.read_text()

old_guard = '''# All old uses of pathReliability were pitch-coordinate uses. Keep that semantic
# explicit now that cleanliness has a separate authority function.
if 'pathReliability(' in cpp:
    raise RuntimeError('path-role implementation left an unexpected pathReliability call')
'''
new_guard = '''# All old uses of pathReliability are pitch-coordinate uses. Their exact
# call-sites are transformed below; the final sweep happens only after those
# anchored transformations have completed.
'''
if source.count(old_guard) != 1:
    raise RuntimeError('path-role wrapper could not find early guard')
source = source.replace(old_guard, new_guard, 1)

anchor = '''# ---------------------------------------------------------------------------
# Functional regressions: (1) formant-shaped noise may be energetic/resonant but
'''
insert = '''# Any detector-only pitch-authority call-site not covered by the anchored
# replacements above inherits the new explicit pitch role. At this point the
# pathReliability implementation itself is already gone, so this cannot rename
# a definition accidentally.
cpp = cpp.replace('pathReliability(', 'pathPitchAuthority(')
if 'pathReliability(' in cpp:
    raise RuntimeError('legacy pathReliability survived final path-role sweep')

# ---------------------------------------------------------------------------
# Functional regressions: (1) formant-shaped noise may be energetic/resonant but
'''
if source.count(anchor) != 1:
    raise RuntimeError('path-role wrapper could not find final sweep anchor')
source = source.replace(anchor, insert, 1)

# The rapid-F0 probe is applied after the path-role refinement. Insert these
# regressions at the stable stale-memory anchor that already exists after the
# base/refine materializers; ProbeRapidF0DetectorV1 will then insert its own
# rapid tests immediately before the same anchor.
old_test_anchor = "anchor = '''    // RAPID_F0_OBSERVATION_MUST_PRECEDE_HOLD_V1\\n'''"
new_test_anchor = "anchor = '''    // OBSERVATION_MEMORY_IS_FALSIFIABLE_V1: emulate a stale wrong register\\n'''"
if source.count(old_test_anchor) != 1:
    raise RuntimeError('path-role wrapper could not find regression anchor declaration')
source = source.replace(old_test_anchor, new_test_anchor, 1)

# QUARTER_PATH_UPPER_COORDINATE_ROLLOFF_V1
# The boundary-bias diagnostic shows that around a true 456 Hz the half-rate
# path is essentially unbiased while the quarter-rate path is about +20 cents
# high. Quarter remains valuable for low/mid F0 and for the ~366 Hz 1/3-family
# evidence used by the direct-high resolver, so retain full pitch authority
# through 390 Hz and roll only its upper coordinate ownership down by 450 Hz.
# This changes detector coordinate weighting only: cleanliness authority,
# validity, observation cadence and all musical/audio authority stay unchanged.
old_quarter_authority = "        case 2: return bandWeight(frequencyHz,  28.0f,  42.0f,  360.0f,  520.0f);"
new_quarter_authority = """        // QUARTER_PATH_UPPER_COORDINATE_ROLLOFF_V1
        case 2: return bandWeight(frequencyHz,  28.0f,  42.0f,  390.0f,  450.0f);"""
if source.count(old_quarter_authority) != 1:
    raise RuntimeError(
        f'quarter pitch-authority anchor: expected one, found {source.count(old_quarter_authority)}')
source = source.replace(old_quarter_authority, new_quarter_authority, 1)

# HALF_PRIMARY_NORMAL_VOICE_COORDINATE_V1
# After the quarter rolloff, every remaining 456-Hz output excursion above the
# musical Hold boundary tracks the full-rate path almost one-for-one, while the
# half-rate path stays within -0.15 cent on average and never crosses the
# boundary. The original role contract already names half-rate as the primary
# normal singing-F0 path. Make that hierarchy explicit for coordinate steering:
# full-rate retains broadband/cleanliness evidence, but contributes only 35% of
# normal-band coordinate authority, smoothly regaining full ownership from
# 720 to 900 Hz and remaining fully authoritative for the >900-Hz direct-F0 case.
old_full_authority = "        case 0: return bandWeight(frequencyHz, 135.0f, 185.0f, 1250.0f, 2400.0f);"
new_full_authority = """        case 0:
        {
            // HALF_PRIMARY_NORMAL_VOICE_COORDINATE_V1
            const float broadbandAuthority = bandWeight(
                frequencyHz, 135.0f, 185.0f, 1250.0f, 2400.0f);
            const float normalVoiceDeference = 0.35f
                + 0.65f * smoothStep(720.0f, 900.0f, frequencyHz);
            return broadbandAuthority * normalVoiceDeference;
        }"""
if source.count(old_full_authority) != 1:
    raise RuntimeError(
        f'full normal-voice authority anchor: expected one, found {source.count(old_full_authority)}')
source = source.replace(old_full_authority, new_full_authority, 1)

namespace = {'__file__': str(script_path), '__name__': '__main__'}
exec(compile(source, str(script_path), 'exec'), namespace)
