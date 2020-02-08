from pathlib import Path
import subprocess
import os
import json
import hashlib
import sys
import base64
import re

if len(sys.argv) < 2:
    print("Argument error, expecting 'compile_shaders.py [Debug|Release|RelWithDebInfo]'")

config = sys.argv[1] # { Debug, Release, RelWithDebInfo }

script_path = os.path.dirname(sys.argv[0])
project_dir = Path(script_path) / ".."

input_dir =  project_dir / "Source" / "Samples" / "MainSample" / "Shaders" # could be passed as argument
output_dir = project_dir / "Build" / "Source" / "Samples" / "MainSample" / config # could be passed as argument (executable path)
shader_cache_path = project_dir / "Build" / "shader_cache_{}.json".format(config.lower())

def append_includes(file_path_str):
    contents = ""
    with open(file_path_str, 'r') as f:
        contents = f.read()
    traversed_inputs = []
    return append_includes_internal(get_includes(file_path_str), contents, traversed_inputs)

def append_includes_internal(includes, contents, traversed_inputs):
    if not includes:
        return contents

    # we could append where the include is but it doesn't matter
    # since we just want to know if all of this changed
    for include in includes:
        include_path = input_dir / include
        if include_path not in traversed_inputs: # prevent infinite recursion
            traversed_inputs.append(str(include_path))
            with open(include_path, 'r') as f:
                contents += f.read()
            contents = append_includes_internal(get_includes(include_path), contents, traversed_inputs)
    
    return contents

def hash_glsl_file(file_path_str):
    return hash_str(append_includes(file_path_str))

def hash_str(s):
    hasher = hashlib.md5()
    hasher.update(s.encode())
    return base64.b64encode(hasher.digest()).decode("utf-8")

def generate_cache_item(input_path_str, output_path_str):
    return {
        "input": hash_str(input_path_str),
        "output": str(Path(output_path_str).resolve()),
        "checksum": hash_glsl_file(input_path_str)
    }

def get_includes(input_path_str):
    # open file
    contents = ""
    with open(input_path_str, "r") as f:
        contents = f.read()

    # look for #include "something.glsl"
    m = re.findall(r'#include\s*"([a-zA-Z0-9_]+.(?:glsl))"', contents)
    if not m:
        return []
    return m

def is_target_up_to_date(shader_cache, cache_item):
    input = cache_item["input"]
    if input not in shader_cache:
        return False

    output = cache_item["output"]
    previous_item = shader_cache[input]

    return input in shader_cache and \
        Path(output).is_file() and \
        previous_item["output"] == cache_item["output"] and \
        previous_item["checksum"] == cache_item["checksum"]

def shader_path_to_output(shader_path):
    shader_name = shader_path.split(os.path.sep)[-1]
    shader_name = shader_name.replace(".", "_")
    output_file = Path(output_dir) / (shader_name + ".spv")
    return str(output_file)

def build_shader(shader_path, output_path):
    print("[build] Building shader '{}' -> '{}'".format(shader_path, output_path))
    command = ["glslc.exe", shader_path, "-o", str(output_path)]
    try:
        subprocess.check_call(command, stderr=subprocess.STDOUT)
        return True
    except:
        exit(1)

def load_shader_cache():
    if not shader_cache_path.is_file():
        return {}
    try:
        with open(str(shader_cache_path)) as f:
            return json.load(f)
    except:
        os.remove(str(shader_cache_path))
        return {}

def save_shader_cache(shader_cache):
    with open(str(shader_cache_path), 'w') as f:
        json.dump(shader_cache, f)

def build_shaders(shaders_paths, shader_cache):
    for shader in shaders_paths:
        input_file_str = str(shader)
        output_file_str = shader_path_to_output(input_file_str)
        c = generate_cache_item(input_file_str, output_file_str)
        if is_target_up_to_date(shader_cache, c):
            print("[skip] Already up to date: {}".format(str(shader)))
        else:
            if build_shader(str(shader), output_file_str):
                shader_cache[c["input"]] = c

# --- main -- #

fragment_shaders = Path(input_dir).glob('**/*.frag')
vertex_shaders = Path(input_dir).glob('**/*.vert')

shader_cache = load_shader_cache()
if shader_cache:
    print("[info] Using shader cache found in '{}'".format(str(shader_cache_path.relative_to(script_path))))
else:
    print("[info] Shader cache not found, generating one in '{}'".format(str(shader_cache_path.relative_to(script_path))))

build_shaders(fragment_shaders, shader_cache)
build_shaders(vertex_shaders, shader_cache)

save_shader_cache(shader_cache)
