# peatitle

A small terminal program that shows large pixel-font text in the center of the
screen, in the spirit of [peaclock](https://github.com/octobanana/peaclock).
Written in C, no dependencies besides libc.

- Text is scaled to fill the terminal and re-fitted when it is resized.
- Long titles are wrapped on word boundaries when that allows a bigger size.
- Uses the terminal's own foreground and background colors, so it follows
  your color scheme.
- Latin and Cyrillic letters, digits and common punctuation are supported.
  Letters are always drawn in upper case.
- Multiple lines are shown one after another in an endless loop.

## Build

    make

Requires `gcc` and `make`.

## Install

    doas make install

Installs `peatitle` to `/usr/local/bin`. Use `PREFIX` and `DESTDIR` to change
the location:

    doas make install PREFIX=/usr

Other targets:

    make clean        remove build files
    doas make uninstall

## Usage

    peatitle [-g TEXT] [-s TEXT] [-w SECONDS]
    peatitle -h
    peatitle -v

| Option       | Description                                                   |
|--------------|---------------------------------------------------------------|
| `-g TEXT`    | Title. `\n` starts a new line; the lines are shown one by one.|
| `-s TEXT`    | Subtitle, drawn in normal text under the title. Same rules.   |
| `-w SECONDS` | Delay between lines. Default is 1. Fractions are allowed.     |
| `-h`         | Show help.                                                    |
| `-v`         | Show version.                                                 |

Press `q`, `Esc` or `Ctrl-C` to quit.

Spaces do not split lines, only `\n` does. Use `\\` for a literal backslash.
The title and the subtitle cycle independently with the same delay.

### Examples

    peatitle -g "Hello"
    peatitle -g "a\nb"
    peatitle -g "text1\ntext2\ntext3" -w 0.5
    peatitle -g "Привет, мир" -s "Тест"

Note that a shell does not turn `\n` into a newline inside double quotes, which
is exactly what peatitle expects.

## Notes

- The terminal must use UTF-8. Half-block characters are used to get finer
  scaling steps.
- Characters missing from the built-in font are drawn as an empty box.
