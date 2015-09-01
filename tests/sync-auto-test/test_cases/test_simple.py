#coding: utf-8

import os
import time
import shutil
from . import test_util

def test_add_file():
    with open(os.path.join(test_util.worktree1, 'a.md'), 'w') as fd:
        fd.write('add a file')
    test_util.verify_result()

def test_add_dir():
    os.mkdir(os.path.join(test_util.worktree1, 'ad'))
    test_util.verify_result()

def test_modify_file():
    with open(os.path.join(test_util.worktree1, 'a.md'), 'w') as fd:
        fd.write('modify a.md')
    test_util.verify_result()

def test_rm_file():
    os.remove(os.path.join(test_util.worktree1, 'a.md'))
    test_util.verify_result()

def test_rm_dir():
    os.rmdir(os.path.join(test_util.worktree1, 'ad'))
    test_util.verify_result()

def test_rename_file():
    with open(os.path.join(test_util.worktree2, 'b.md'), 'w') as fd:
        fd.write('add b.md')
    time.sleep(1)
    os.rename(os.path.join(test_util.worktree2, 'b.md'), os.path.join(test_util.worktree2, 'b_bak.md'))
    test_util.verify_result()

def test_rename_dir():
    os.mkdir(os.path.join(test_util.worktree2, 'ab'))
    time.sleep(1)
    os.rename(os.path.join(test_util.worktree2, 'ab'), os.path.join(test_util.worktree2, 'ab_bak'))
    test_util.verify_result()

def test_each():
    os.mkdir(os.path.join(test_util.worktree1, 'abc'))
    with open(os.path.join(test_util.worktree1, 'abc', 'c.md'), 'w') as fd:
        fd.write('add abc/c.md')
    time.sleep(1)

    os.mkdir(os.path.join(test_util.worktree2, 'bcd'))
    with open(os.path.join(test_util.worktree2, 'bcd', 'd.md'), 'w') as fd:
        fd.write('add bcd/d.md')
    test_util.verify_result()

def test_unsync_resync():
    test_util.desync_cli1()
    shutil.rmtree(os.path.join(test_util.worktree1, 'abc'))
    with open(os.path.join(test_util.worktree1, 'bcd', 'd.md'), 'w') as fd:
        fd.write('modify bcd/d.md to test unsync resync')
    test_util.sync_cli1()

    test_util.verify_result()

    if not os.path.exists(os.path.join(test_util.worktree1, 'abc')):
        assert False, 'dir abc should be recreated when resync'

    if len(os.listdir(os.path.join(test_util.worktree1, 'bcd'))) != 2:
        assert False, 'should generate conflict file for bcd/d.md when resync'

def test_modify_timestamp():
    os.utime(os.path.join(test_util.worktree1, 'bcd', 'd.md'), None)
    test_util.verify_result()
