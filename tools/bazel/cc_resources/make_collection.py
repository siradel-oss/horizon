import sys
import jinja2
from python.runfiles import Runfiles

from pathlib import Path

CC_PATH = sys.argv[1]
H_PATH = sys.argv[2]
NAMESPACE = sys.argv[3]
KEYS = sys.argv[4:]

values = {}
values["namespace"] = NAMESPACE
values["keys"] = KEYS
values["header"] = Path(H_PATH).name

r = Runfiles.Create()
template_dir = Path(r.Rlocation("horizon/tools/bazel/cc_resources/collection.tpl.cpp")).parent

loader = jinja2.FileSystemLoader(template_dir)
tpl_env = jinja2.Environment(loader = loader)
tpl_env.trim_blocks = True
tpl_env.lstrip_blocks = True

tpl_cc = tpl_env.get_template("collection.tpl.cpp")
tpl_h = tpl_env.get_template("collection.tpl.h")

fp = open(CC_PATH, "w+")
fp.write(tpl_cc.render(values))
fp.close()

fp = open(H_PATH, "w+")
fp.write(tpl_h.render(values))
fp.close()
