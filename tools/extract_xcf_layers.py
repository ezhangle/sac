#!/usr/bin/env python
# coding: utf-8

import os, glob, sys, time

allowedFormat = [".xcf", ".psd"]

def run(directory, rewrite = False):
	from gimpfu import pdb
	start = time.time()
	#for all xcf files in working directory
	print("Running on directory '{0}'".format(directory))
	for filename in os.listdir(directory):
		if filename[-4:] in allowedFormat:
			print("Found a file : '{0}'".format(filename))
			image = pdb.gimp_file_load(os.path.join(directory, filename), os.path.join(directory, filename))

			with open(os.path.join(directory, os.path.splitext(filename)[0]+".desc"), "wb") as f:
				for layer in image.layers:
					for c in [layer] + layer.children:
						f.write("texture = {}\n"\
								"size = {}, {}\n"\
								"position = {}, {}\n".format(c.name, c.width, c.height, *c.offsets))

						print("Write layer '{0}' in '{0}.png'".format(c.name))
						# write the new png file
						pdb.file_png_save(image, c, os.path.join(directory, c.name +".png"), 
													os.path.join(directory, c.name +".png"), 
													0, 9, 0, 0, 0, 0, 0)

	end = time.time()
	print("Finished, total processing time : {0:.{1}f}".format(end-start, 2))


if __name__ == '__main__':
	directory = os.getcwd() + '/'
	script_directory = os.path.abspath(os.path.dirname(sys.argv[0]))
	print("This script will convert a (multilayer) xcf of current directory into as many as needed png files.")
	print("Let's go !")
	# execute gimp with this script
	os.chdir(script_directory)
	command = 'gimp -idfs --batch-interpreter python-fu-eval -b '\
			  '"import sys; sys.path=[\'.\']+sys.path;'\
			  'import extract_xcf_layers as e; e.run(\''+directory+'\')" -b "pdb.gimp_quit(1)"'
	os.system(command)
	os.chdir(directory)

	print("Alright, auf wiedersehen !")
