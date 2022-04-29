Each script is a pipe from stdin to stdout written in sed. All of them
composed in order transform an individual manual page in nroff format
to a similar looking responsive html page using the Bootstrap
framework. Styling is hard coded except for a few settings defined in
cmake/configure_manpages.cmake.in. These scripts aren't designed to
cope with all aspects of nroff format, but only to work on the subset
of nroff used for manpages in this repo under the man/ directory.
