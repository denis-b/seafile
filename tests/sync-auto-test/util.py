#coding: utf-8

import os
import shutil
import time
import subprocess
from setting import Setting
import seaf_op

def call_process(params):
    with open(os.devnull, 'w') as fd:
        ret = subprocess.check_output(params, stderr=fd)
    return ret

class TestUtil():
    def __init__(self):
        self.setting = Setting()
        self.cli1_dir = os.path.join(os.getcwd(), 'cli1')
        self.cli2_dir = os.path.join(os.getcwd(), 'cli2')
        self.worktree1 = os.path.join(os.getcwd(), 'worktree1')
        self.worktree2 = os.path.join(os.getcwd(), 'worktree2')
        self.enc_repo = False
        try:
            self.enc_repo = bool(os.environ['ENCRYPTED_REPO'])
        except Exception:
            pass
        self.repo_id = None

    def init_conf(self):
        self.setting.parse_config()
        if os.path.exists(self.cli1_dir):
            shutil.rmtree(self.cli1_dir)
        if os.path.exists(self.cli2_dir):
            shutil.rmtree(self.cli2_dir)
        if os.path.exists(self.worktree1):
            shutil.rmtree(self.worktree1)
        if os.path.exists(self.worktree2):
            shutil.rmtree(self.worktree2)

        os.mkdir(self.worktree1)
        os.mkdir(self.worktree2)

        seaf_op.seaf_init(self.cli1_dir)
        seaf_op.seaf_init(self.cli2_dir)

    def start_daemon(self):
        seaf_op.seaf_start_all(self.cli1_dir)
        seaf_op.seaf_start_all(self.cli2_dir)

    def create_repo(self):
        self.repo_id = seaf_op.seaf_create(self.cli1_dir, self.setting.server_url,
                                           self.setting.user, self.setting.password,
                                           self.enc_repo)

    def sync_cli1(self):
        seaf_op.seaf_sync(self.cli1_dir, self.setting.server_url, self.repo_id,
                          self.worktree1, self.setting.user, self.setting.password)

    def sync_cli2(self):
        seaf_op.seaf_sync(self.cli2_dir, self.setting.server_url, self.repo_id,
                          self.worktree2, self.setting.user, self.setting.password)

    def sync_repo(self):
        self.sync_cli1()
        self.sync_cli2()

    def desync_cli1(self):
        seaf_op.seaf_desync(self.cli1_dir, self.worktree1)

    def desync_cli2(self):
        seaf_op.seaf_desync(self.cli2_dir, self.worktree2)

    def desync_repo(self):
        self.desync_cli1()
        self.desync_cli2()
        # delete test repo
        seaf_op.seaf_delete(self.cli1_dir, self.setting.server_url,
                            self.setting.user, self.setting.password,
                            self.repo_id)

    def stop_daemon(self):
        seaf_op.seaf_stop(self.cli1_dir)
        seaf_op.seaf_stop(self.cli2_dir)

    def clean(self):
        try:
            shutil.rmtree(self.cli1_dir)
            shutil.rmtree(self.cli2_dir)
            shutil.rmtree(self.worktree1)
            shutil.rmtree(self.worktree2)
        except Exception:
            pass

    @staticmethod
    def verify_by_rsync(dir1, dir2):
        ret = call_process(['rsync', '-acrin', dir1, dir2])
        if ret:
            for d in ret.split('\n'):
                # omit empty str
                if not d:
                    continue
                # omit directory has almost same result except st_mod
                dattr = d.split(' ')[0]
                if dattr != '.d..t......':
                    assert False, 'Sync with two client have different result: %s' % dattr

    def verify_result(self, callable=None, duration=60):
        time.sleep(duration)
        if callable:
            callable(self.worktree1, self.worktree2)
        else:
            dir1 = os.path.join(self.worktree1, '')
            dir2 = os.path.join(self.worktree2, '')
            TestUtil.verify_by_rsync(dir1, dir2)
            TestUtil.verify_by_rsync(dir2, dir1)
