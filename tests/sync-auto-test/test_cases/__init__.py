from util import TestUtil

test_util = TestUtil()

print 'hello ...'

def setup():
    # init sync related stuff
    print 'running setup ...'
    test_util.init_conf()
    test_util.start_daemon()
    test_util.create_repo()
    test_util.sync_repo()
    print '\n----------------------------------------------------------------------'

def teardown():
    # clean sync related stuf
    print '----------------------------------------------------------------------\n'
    test_util.desync_repo()
    test_util.stop_daemon()
    test_util.clean()
