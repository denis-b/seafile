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

        if os.name == 'nt':
            conf2 = os.path.join(self.cli2_dir, 'ccnet.conf')
            with open(conf2, 'r') as fp:
                contents = fp.read()
            with open(conf2, 'w') as fp:
                fp.write(contents.replace('PORT = 13419', 'PORT = 13420'))

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

    def wait_sync(self):
        max_retry = 12
        cur_retry = 0
        while cur_retry < max_retry:
            time.sleep(5)
            cur_retry += 1
            repo1 = seaf_op.seaf_get_repo(self.cli1_dir, self.repo_id)
            if repo1 is None:
                continue
            repo2 = seaf_op.seaf_get_repo(self.cli2_dir, self.repo_id)
            if repo2 is None:
                continue
            if repo1.head_cmmt_id == repo2.head_cmmt_id:
                break

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
                # rsync 3.1.1 : '.d..t.......'
                # rsync 3.0.9 : '.d..t......'
                if not all([c in ('d', 't', '.') for c in dattr]):
                    assert False, 'Sync with two client have different result: %s' % dattr

    def verify_result(self, callable=None):
        self.wait_sync()
        if callable:
            callable(self.worktree1, self.worktree2)
        else:
            dir1 = os.path.join(self.worktree1, '')
            dir2 = os.path.join(self.worktree2, '')
            TestUtil.verify_by_rsync(dir1, dir2)
            TestUtil.verify_by_rsync(dir2, dir1)
