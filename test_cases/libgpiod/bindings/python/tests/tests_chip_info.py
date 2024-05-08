# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2022 Bartosz Golaszewski <brgl@bgdev.pl>

import gpiod

from . import gpiosim
from unittest import TestCase


class ChipInfoProperties(TestCase):
    def setUp(self):
        self.sim = gpiosim.Chip(label="foobar", num_lines=16)
        self.chip = gpiod.Chip(self.sim.dev_path)
        self.info = self.chip.get_info()

    def tearDown(self):
        self.info = None
        self.chip.close()
        self.chip = None
        self.sim = None

    def test_chip_info_name(self):
        self.assertEqual(self.info.name, self.sim.name)

    def test_chip_info_label(self):
        self.assertEqual(self.info.label, "foobar")

    def test_chip_info_num_lines(self):
        self.assertEqual(self.info.num_lines, 16)

    def test_chip_info_properties_are_immutable(self):
        with self.assertRaises(AttributeError):
            self.info.name = "foobar"

        with self.assertRaises(AttributeError):
            self.info.num_lines = 4

        with self.assertRaises(AttributeError):
            self.info.label = "foobar"


class ChipInfoStringRepresentation(TestCase):
    def test_chip_info_str(self):
        sim = gpiosim.Chip(label="foobar", num_lines=16)

        with gpiod.Chip(sim.dev_path) as chip:
            info = chip.get_info()

            self.assertEqual(
                str(info),
                '<ChipInfo name="{}" label="foobar" num_lines=16>'.format(sim.name),
            )
