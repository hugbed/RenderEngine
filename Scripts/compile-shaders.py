import re
import sys
import json
import base64
import hashlib
import subprocess
import os

def get_includes_directives(shader_str):
	""" look for #include "something.glsl" """
	m = re.findall(r'#include\s*"([a-zA-Z0-9_]+.(?:glsl))"', shader_str)
	return m if m else []

def get_files_in_directory(path, extensions):
	files = []
	for f in os.listdir(path):
		if not os.path.isfile(os.path.join(path, f)):
			continue

		if not extensions or os.path.splitext(f)[1][1:] in extensions:
			files.append(f)

	return files

def build_file_inclusion_graph(directory, files):
	""" Build a list of files into which each file is included, of the form:
		 -> { 'file': [ "included_in1", "included_in2" ] }
		In this example, when "file" changes, you need to rebuild "included_in1", "included_in2"
	"""
	files_that_include = {}
	for file in files:
		with open(os.path.join(directory, file), 'r') as f:
			contents = f.read()
		
		# if this file changes, we need to rebuild it
		files_that_include[file] = []

		# if any of these includes change, we need to rebuild this file
		include_directives = get_includes_directives(contents)
		for included_file in include_directives:
			files_that_include.setdefault(included_file, []).append(file)

	return files_that_include

def get_files_to_rebuild(files_that_include, files_changed):
	files_to_rebuild = set()
	for file in files_changed:
		# build file
		files_to_rebuild.add(file)

		# build other files that include this one
		if file in files_that_include:
			files_to_rebuild = files_to_rebuild.union(set(files_that_include[file]))

	return list(files_to_rebuild)

def get_str_hash(str):
	hasher = hashlib.md5()
	hasher.update(str.encode())
	return base64.b64encode(hasher.digest()).decode("utf-8")

def get_file_hashes(directory, files):
	""" returns { "filename": "SOME_BASE_64_HASH", ... } """
	file_to_hash = {}
	for file in files:
		with open(os.path.join(directory, file), 'r') as f:
			file_to_hash[file] = get_str_hash(f.read())

	return file_to_hash

def get_difference(old_map, new_map):
	diffs = []
	for key, value in new_map.items():
		if key not in old_map or old_map[key] != value:
			diffs.append(key)

	return diffs

def build_shader(shader_path, output_path):
	command = ["glslc.exe", shader_path, "-o", str(output_path)]
	subprocess.check_call(command, stderr=subprocess.STDOUT)

def shader_filename_to_spv(file_path):
	[ name, ext ] = os.path.splitext(file_path)
	return "{}_{}.spv".format(name, ext[1::])

# --------- Tests --------- #

import unittest

class TestStuff(unittest.TestCase):
	def test_include_directires(self):
		shader_test = """
			#include "something.glsl"
			#include "something_else.glsl"
			float somecode() {}
		"""
		expected_includes = ["something.glsl", "something_else.glsl"]
		res_includes = get_includes_directives(shader_test)
		self.assertEqual(expected_includes, res_includes)

	def test_path_in_directory(self):
		directory_test = "F:\\Personal\\RenderEngine\\Scripts"
		res = get_files_in_directory(directory_test)
		self.assertEqual(res, ["compile-shaders.py", "compile-shaders2.py"])

	def test_dependency_graph(self):
		base_path = "F:\\Personal\\RenderEngine\\Source\\Samples\\MainSample\\Shaders"
		files = get_files_in_directory(base_path)
		files_that_include = build_file_inclusion_graph(base_path, files)
		#print(files_that_include)

	def test_get_file_hashes(self):
		files_that_include = { "a": ["b", "c"], "d": ["e"] }
		self.assertEqual(get_files_to_rebuild(files_that_include, ["a"]), ["b", "c"])
		self.assertEqual(get_files_to_rebuild(files_that_include, ["b"]), [])
		self.assertEqual(get_files_to_rebuild(files_that_include, ["c"]), [])
		self.assertEqual(get_files_to_rebuild(files_that_include, ["d"]), ["e"])

	def test_get_file_hashes(self):
		base_path = "F:\\Personal\\RenderEngine\\Source\\Samples\\MainSample\\Shaders"
		files = get_files_in_directory(base_path)
		hashes = get_file_hashes(base_path, files)
		# print(hashes)

	def test_difference(self):
		self.assertEqual(get_difference(
			{ "a": "1", "b": "2", "c": "3" },
			{ "a": "2", "b": "2", "c": "3" }
		), ["a"])
		self.assertEqual(get_difference(
			{ "a": "1", "b": "2", "c": "3" },
			{ "d": "0" }
		), ["d"])

if __name__ == '__main__':
	#if len(sys.argv) < 2:
	#	print("Argument error, expecting 'compile_shaders2.py path_to_shaders output_path'")
	#	exit(1)
	script_path = os.path.dirname(os.path.realpath(__file__))

	shaders_path = sys.argv[1] # e.g. "F:\\Personal\\RenderEngine\\Source\\Samples\\MainSample\\Shaders"
	output_path = sys.argv[2] # e.g. "F:\\Personal\\RenderEngine\\Build\\Source\\Samples\\MainSample"

	print("[SPIRV] Reading shaders from: {}".format(shaders_path))
	print("[SPIRV] Outputing shaders to: {}".format(output_path))

	# Load files in directory
	files_in_directory = get_files_in_directory(shaders_path, ["glsl", "frag", "vert"])
	if not files_in_directory:
		print("No files to build")
		exit(0)

	# Load previous file hashes
	last_file_hashes = {}
	config_file = os.path.join("F:\\Personal\\RenderEngine\\Scripts", "..", "Build", "config.json") # todo: replace hardcoded
	if os.path.exists(config_file):
		with open(config_file) as f:
			config = json.load(f)
			if shaders_path in config:
				last_file_hashes = config[shaders_path]

	# Clear hash of files that do not exist anymore
	for filename, file_hash in last_file_hashes.items():
		file_output = os.path.join(output_path, shader_filename_to_spv(filename))
		has_output = os.path.splitext(filename)[1] in [ ".frag", ".vert" ]
		if has_output and not os.path.exists(file_output):
			last_file_hashes[filename] = ""

	# Find files that have changed
	current_file_hashes = get_file_hashes(shaders_path, files_in_directory)
	file_diffs = get_difference(last_file_hashes, current_file_hashes)

	# Find files to rebuild
	files_that_include = build_file_inclusion_graph(shaders_path, files_in_directory)
	files_to_build = get_files_to_rebuild(files_that_include, file_diffs)

	# Filter-out glsl files, don't need to build those
	files_to_build[:] = filter(lambda f: os.path.splitext(f)[1] != ".glsl" , files_to_build)
	files_in_directory[:] = filter(lambda f: os.path.splitext(f)[1] != ".glsl" , files_in_directory)

	# Output files that are already up to date
	for file in files_in_directory:
		if file not in files_to_build:
			print("[SPIRV] Skipped, already up to date: {} -> {}".format(file, shader_filename_to_spv(file)))

	# Build shaders
	for file in files_to_build:
		file_spv = shader_filename_to_spv(file)
		print("[SPIRV] Building shader '{}' -> '{}'".format(file, file_spv))
		shader_in = os.path.join(shaders_path, file)
		shader_out = os.path.join(output_path, file_spv)
		build_shader(shader_in, shader_out)

	# Save current hashes
	with open(config_file, 'w') as f:
		json.dump({ shaders_path : current_file_hashes }, f)

	# unittest.main()
