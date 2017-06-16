#! /usr/bin/env python
# encoding: utf-8

import re

top = '.'
out = 'build'
APPNAME = 'live-cue'
VERSION = '0.1.0'


def options(opt):
    opt.load('compiler_c')
    opt.add_option('--lv2dir',
                   action='store',
                   default='/usr/lib/lv2',
                   help='The directory where LV2 plugins are to be installed. ' +
                   'Used together with install command.')


def configure(ctx):
    ctx.load('compiler_c')
    ctx.env.LV2DIR = ctx.options.lv2dir


def build(bld):
    bundle = 'live-cue.lv2'
    install_path = '%s/%s' % (bld.env.LV2DIR, bundle)

    module_path = re.sub('^lib', '', bld.env.cshlib_PATTERN)
    module_ext = module_path[module_path.rfind('.'):]

    compile_lib = bld(features='c cshlib',
                      source='live-cue.c',
                      target='%s/live-cue' % bundle,
                      install_path=install_path)
    compile_lib.env.cshlib_PATTERN = module_path
    bld(features='subst',
        source='manifest.ttl.in',
        target='%s/manifest.ttl' % bundle,
        install_path=install_path,
        LIB_EXT=module_ext)
    bld(features='subst',
        is_copy=True,
        source='live-cue.ttl',
        target='%s/live-cue.ttl' % bundle,
        install_path=install_path)


def dist(ctx):
    ctx.excl = '**/.waf* **/.lock-waf* **/.git **/.vscode **/.gitignore'
