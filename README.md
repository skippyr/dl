<p align="center">
  <img alt="" src="assets/ornament.webp" />
</p>
<h1 align="center">≥v≥v&ensp;dl&ensp;≥v≥v</h1>
<p align="center">
  <img alt="" src="https://img.shields.io/github/license/skippyr/dl?style=plastic&label=%E2%89%A5%20license&labelColor=%2324130e&color=%23b8150d" />
  &nbsp;
  <img alt="" src="https://img.shields.io/github/v/tag/skippyr/dl?style=plastic&label=%E2%89%A5%20tag&labelColor=%2324130e&color=%23b8150d" />
  &nbsp;
  <img alt="" src="https://img.shields.io/github/commit-activity/t/skippyr/dl?style=plastic&label=%E2%89%A5%20commits&labelColor=%2324130e&color=%23b8150d" />
  &nbsp;
  <img alt="" src="https://img.shields.io/github/stars/skippyr/dl?style=plastic&label=%E2%89%A5%20stars&labelColor=%2324130e&color=%23b8150d" />
</p>

## ❡ About

A simple directory listing utility for Linux.

<p align="center">
  <img alt="" src="assets/preview.webp" />
</p>
<p align="center"><sup><strong>Caption:</strong> a preview of <code>dl</code> running on a terminal. The terminal theme used is <a href="https://github.com/skippyr/flamerial">flamerial</a> and font is <a href="https://www.nerdfonts.com/font-downloads">firacode nerd font</a>.</sup></p>

## ❡ Install

### Dependencies

The following dependencies must be installed before installing it:

- **git**: it will be used to clone this repository.
- **gcc**, **make**: they will be used to compile this software.
- **Noto Sans**, [**Nerd Font Symbols**](https://www.nerdfonts.com/font-downloads): these fonts provide the pretty symbols used in the software.
- [**libtdk**](https://github.com/skippyr/libtdk): it is a library required as dependency.

### Procedures

To install this software, using a terminal, follow these steps:

- Clone this repository using `git`:

```sh
git clone --depth 1 https://github.com/skippyr/dl;
```

- Access the directory of the repository you cloned using `cd`:

```sh
cd dl;
```

- Use `make` to compile and install this software:

```sh
make install;
```

- Add the following environment variables to your shell startup file in order to include the installed files that are at the directories under `~/.local/share`:

```zsh
export PATH=${PATH}:~/.local/share/bin;
export MANPATH=${PATH}:~/.local/share/man;
```

- Open a new shell session.

## ❡ Uninstall

To uninstall this software, using a terminal, follow these steps:

- Go back to the directory of the repository you cloned.
- Use `make` to uninstall it:

```sh
make uninstall;
```

## ❡ Documentation

After installed, you can access its documentation using `man`:

```sh
man dl.1;
```

## ❡ Help

If you need help related to this project, open a new issue in its [issues pages](https://github.com/skippyr/dl/issues) or send me an [e-mail](mailto:skippyr.developer@gmail.com) describing what is going on.

## ❡ Contributing

This project is open to review and possibly accept contributions, specially fixes and suggestions. If you are interested, send your contribution to its [pull requests page](https://github.com/skippyr/dl/pulls) or to my [e-mail](mailto:skippyr.developer@gmail.com).

By contributing to this project, you agree to license your work under the same license that the project uses.

## ❡ License

This is free software licensed under the BSD-3-Clause License that comes WITH NO WARRANTY. Refer to the `LICENSE` file that comes in its source code for license and copyright details.

&ensp;
<p align="center"><sup><strong>≥v≥v&ensp;Here Be Dragons!&ensp;≥v≥</strong><br />Made with love by skippyr <3</sup></p>
