"""Cold reprogram may supersede a stale Host record, never a live same-run owner."""
import json
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
import run_z7_nano_usb_live as live

class ReachedBuildCheck(Exception):pass

class HostRestart(unittest.TestCase):
    def run_case(self,previous,blocked):
        records={"active_host.json":previous,
                 "program.json":{"status":"passed","attempt":"new-program","cable_serial":"210241277120"},
                 "active_app.json":{"status":"ready","program_attempt":"new-program"}}
        with patch.object(Path,"exists",return_value=True), \
             patch.object(Path,"read_text",lambda p,**kw:json.dumps(records[p.name])), \
             patch.object(sys,"argv",["run_live","start"]), \
             patch.object(live.display,"checked_app",side_effect=ReachedBuildCheck) as build:
            with self.assertRaises(RuntimeError if blocked else ReachedBuildCheck):live.main()
            self.assertEqual(build.call_count,0 if blocked else 1)
    def test_new_program_same_board_can_revalidate(self):
        self.run_case({"status":"running","display_program_attempt":"old-program","cable_serial":"210241277120"},False)
    def test_same_program_owner_blocks_duplicate_start(self):
        self.run_case({"status":"running","display_program_attempt":"new-program","cable_serial":"210241277120"},True)
    def test_other_board_owner_is_not_discarded(self):
        self.run_case({"status":"running","display_program_attempt":"old-program","cable_serial":"different"},True)
    def test_unknown_owner_remains_blocked(self):
        self.run_case({"status":"start_unconfirmed"},True)
    def test_stopped_owner_may_start(self):
        self.run_case({"status":"stopped"},False)

if __name__=="__main__":unittest.main()
