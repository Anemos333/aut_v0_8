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

namespace = {'__file__': str(script_path), '__name__': '__main__'}
exec(compile(source, str(script_path), 'exec'), namespace)
