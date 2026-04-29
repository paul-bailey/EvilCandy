# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information
import sys
import os

sys.path.insert(0, os.path.abspath('.'))

def setup(app):
    from evc_lexer import EvilCandyLexer, EvilCandyPromptLexer
    app.add_lexer('evilcandy', EvilCandyLexer)
    app.add_lexer('evc-console', EvilCandyPromptLexer)

project = 'EvilCandy'
copyright = '2025, Paul Bailey'
author = 'Paul Bailey'
release = '0.0.1'

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = []

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store', 'Tutorial.rst']

root_doc = 'index'

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

highlight_language = 'evilcandy'
suppress_warnings = ['misc.highlighting_failure']

html_theme = 'alabaster'
html_sidebars = {
    '**': [
        'about.html',
        'navigation.html',
        'relations.html',
    ]
}
html_theme_options = {
    'fixed_sidebar': True,
    'sidebar_collapse': True,
}
html_static_path = ['_static']
html_css_files = ['css/custom.css']
