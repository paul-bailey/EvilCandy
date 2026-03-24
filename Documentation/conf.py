# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'EvilCandy'
copyright = '2025, Paul Bailey'
author = 'Paul Bailey'
release = '0.0.1'

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = []

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store', 'Tutorial.rst']

root_doc = 'contents'

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

# not really js, but I have no desire to create a whole damn
# syntax-highlighting scheme of my own.
highlight_language = 'js'
suppress_warnings = ['misc.highlighting_failure']

html_theme = 'alabaster'
html_static_path = ['_static']
