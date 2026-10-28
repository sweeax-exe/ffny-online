"""defjam-mcp v2: the shared library behind the MCP server (server.py) and the CLI (dj.py).

    machine    paths, the machine-wide test lock, game processes, the safe environment
    roots      disposable data roots (junctions to the dump, small saves copied)
    logparse   pure log parsing: the digest of a log, the per-match analysis of an online test
    copies     a test session: the local relay, game copies, the control channel, window pictures
    scenarios  boot / offline / online / gallery, each ending in a compact JSON result

Standard library only (Pillow for pictures).
"""
