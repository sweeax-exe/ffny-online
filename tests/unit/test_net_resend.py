"""Build 109's connection fixes (community/connection-report-2026-11-22.md): the resend window's numbers in
src/hooks/netplay.c (every STEPS packet carries what this PC holds of each player's entries; a player that lacks
older entries gets them again, up to RESEND_MAX in one packet), and that they fit the relay's limits and cover
the gap a stall leaves (fight-resume-design.md 2.3). The forced-stall run is dj.py scenario online --drop-downlink."""
import re
from pathlib import Path

from server_src import RELAY

ROOT = Path(__file__).resolve().parents[2]
NETPLAY = (ROOT / 'src' / 'hooks' / 'netplay.c').read_text(encoding='utf-8')


def define(name, text=NETPLAY):
    m = re.search(r'#define %s\s+(\w+)' % name, text)
    assert m, name
    return int(m.group(1).rstrip('uU'), 0)


def test_resend_packet_fits_the_payload_limit():
    entry, maxp, resend = define('ENTRY'), 4, define('RESEND_MAX')
    # P_STEPS: type, u32 match, u32 last, u8 count, the entries, then ACK_MARK, a count and (u8 pos, u32 have) each
    size = 1 + 4 + 4 + 1 + resend * entry + 2 + 5 * maxp
    assert size <= 1150, size               # the relay's MaxPayload (relay/protocol.go)
    assert 'MaxPayload  = 1150' in (RELAY / 'protocol.go').read_text(encoding='utf-8') if RELAY else True


def test_resend_covers_the_stall_gap():
    # lockstep: 2 x delay + 1 at the delay's cap (DELAY_MAX + the lockstep margin); rollback: 2 x delay + RB_AHEAD
    delay_max, rb_ahead, resend = define('DELAY_MAX') + 1, define('RB_AHEAD'), define('RESEND_MAX')
    assert 2 * delay_max + 1 <= resend
    assert 2 * define('DELAY_MAX') + rb_ahead <= resend
    assert define('REDUNDANT') < resend


def test_trailer_after_the_entries_and_old_readers_skip_it():
    # every build's store_steps reads `count` entries and ignores what follows (the trailer is its own version mark)
    assert 'if (len < 9 + count * ENTRY) return;' in NETPLAY
    assert 'if (len >= t + 2 && p[t] == ACK_MARK)' in NETPLAY
    assert define('ACK_MARK') == 0xAC
    # the resend goes only to a player whose packets carry the trailer, once its count stood still
    assert '!g_peer_acks[i] || g_peer_ack_match[i] != N.match' in NETPLAY
    assert 'g_peer_ack_seen[i] >= g_peer_ack_at[i] + ACK_STUCK_MS' in NETPLAY
    if RELAY:   # the relay's recording reads count entries and nothing after them
        rec = (RELAY / 'record.go').read_text(encoding='utf-8')
        assert 'len(payload) < 10+count*stepEntry' in rec


def test_stall_notice_and_report():
    for text in ('Connection problem: you are not receiving any data (%u s)',
                 'Connection problem: %s is not sending (%u s)',
                 'Connection problem: %s is not receiving your data (%u s)',
                 'Connection problem: waiting for %s... (%u s)'):
        assert text in NETPLAY
    assert '"conn\\":{\\"stalls\\":%u' in NETPLAY
    assert 'd3d11_translator_present_again();' in NETPLAY
    trans = (ROOT / 'src' / 'hooks' / 'd3d11_translator.c').read_text(encoding='utf-8')
    assert 'void d3d11_translator_present_again(void)' in trans
    if RELAY:   # the account service keeps the "conn" facts
        facts = (RELAY / 'accounts' / 'netfacts.go').read_text(encoding='utf-8')
        assert 'Conn *NetConn `json:"conn,omitempty"`' in facts
