#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2022 Petr Vorel <pvorel@suse.cz>

TST_MOUNT_DEVICE=1
TST_NEEDS_ROOT=1
TST_FS_TYPE=ext4
TST_TESTFUNC=test
TST_CNT=3

test1()
{
	EXPECT_PASS "cd $TST_MNTPOINT"
}

test2()
{
	EXPECT_PASS "grep '$TST_MNTPOINT $TST_FS_TYPE' /proc/mounts"
}

test3()
{
	tst_brk TCONF "quit early to test early tst_umount"
}

. tst_test.sh
tst_run
