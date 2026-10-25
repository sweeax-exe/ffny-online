"""The ffnystudio library: the copy build_addon.py bundles inside the add-on, or the checkout's own.

Running the add-on straight from a checkout (a link to tools/ffny-studio/blender/ffny_studio in Blender's
add-on folder) uses tools/ffny-studio/ffnystudio and scripts/hd_assets.py directly.
"""

try:
    from .ffnystudio import blender_data, game, texture  # noqa: F401  (bundled by build_addon.py)
except ImportError:
    import os
    import sys

    _DEV = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    if os.path.isdir(os.path.join(_DEV, "ffnystudio")) and _DEV not in sys.path:
        sys.path.append(_DEV)
    from ffnystudio import blender_data, game, texture  # noqa: F401
