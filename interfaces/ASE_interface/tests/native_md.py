"""Run short ABACUS-native MD and check its ASE trajectory against MD_dump."""

import os
import unittest
from pathlib import Path

import numpy as np
from ase.build import bulk

from abacuslite import Abacus, AbacusProfile
from common import (assert_native_md_facts, assert_stru_velocity_facts,
                    preserved_tmpdir, test_profile_paths)


class TestNativeMD(unittest.TestCase):
    def test_two_steps(self):
        self.run_md_case(nsteps=2)

    def test_thermodynamic_output_disabled(self):
        self.run_md_case(nsteps=4, thermo_frequency=0)

    def test_sparse_thermodynamic_output(self):
        self.run_md_case(nsteps=4, thermo_frequency=2)

    def run_md_case(self, *, nsteps, thermo_frequency=None):
        command, pporb = test_profile_paths(Path(__file__).parent)
        silicon = bulk('Si', 'diamond', a=5.43)
        silicon.set_velocities(np.array([[0.01, 0.0, 0.0],
                                         [-0.01, 0.0, 0.0]]))
        initial_velocities = silicon.get_velocities().copy()
        legacy = os.environ.get('ASE_ABACUS_FORMAT', 'latest') == 'legacy'
        if legacy:
            from abacuslite.io.legacyio import read_abacus_out
        else:
            from abacuslite.io.latestio import read_abacus_out
        profile = AbacusProfile(command=command, pseudo_dir=pporb,
                                orbital_dir=pporb, omp_num_threads=1)
        with preserved_tmpdir(f'abacus_ase_native_md_thermo_{thermo_frequency}_') as run:
            extra_input = {}
            if thermo_frequency is not None:
                extra_input['md_thermo_freq'] = thermo_frequency
            calculator = Abacus(
                profile=profile,
                directory=run,
                pseudopotentials={'Si': 'Si_ONCV_PBE-1.0.upf'},
                basissets={'Si': 'Si_gga_8au_100Ry_2s2p1d.orb'},
                inp={
                    'calculation': 'md',
                    'basis_type': 'lcao',
                    'gamma_only': True,
                    'scf_thr': 1e-3,
                    'cal_force': True,
                    'md_nstep': nsteps,
                    'md_tchain': 4,
                    'md_tfirst': 10,
                    'md_tfreq': 0.1,
                    'init_vel': 1,
                    **extra_input,
                },
            )
            silicon.calc = calculator
            self.assertTrue(np.isfinite(silicon.get_potential_energy()))
            assert_stru_velocity_facts(self, Path(run) / 'STRU', initial_velocities)
            output = Path(run) / 'OUT.ABACUS'
            frames = read_abacus_out(output / 'running_md.log')
            assert_native_md_facts(self, frames, output / 'MD_dump',
                                   expected_frames=nsteps + 1 if legacy else nsteps)
            if thermo_frequency is not None:
                thermo_tables = (output / 'running_md.log').read_text().count('Energy (Ry)')
                if thermo_frequency == 0:
                    self.assertEqual(thermo_tables, 0)
                else:
                    self.assertGreater(thermo_tables, 0)
                    self.assertLess(thermo_tables, nsteps)
                print(f'MD thermo probe: frequency={thermo_frequency}, '
                      f'thermo_tables={thermo_tables}, parsed_frames={len(frames)}',
                      flush=True)


def load_tests(loader, tests, pattern):
    # Investigation branch: only PR #8160's develop engine accepts the new
    # parameter. LTS retains the original required case without skipped tests.
    if os.environ.get('ASE_ABACUS_FORMAT', 'latest') == 'legacy':
        return unittest.TestSuite([TestNativeMD('test_two_steps')])
    return tests


if __name__ == '__main__':
    unittest.main()
