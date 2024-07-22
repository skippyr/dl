<p align="center">
  <img alt="" src="assets/ornament.png" width=1020 />
</p>
<h1 align="center">≥v≥v&ensp;dl&ensp;≥v≥v</h1>
<p align="center">Directory Lister</p>
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

A simple directory listing utility available for Windows, Linux and MacOS.

<p align="center">
  <img alt="" src="assets/preview-macos.png" width=1020 />
  <img alt="" src="assets/preview-windows.png" width=1020 />
</p>
<p align="center"><strong>Caption:</strong> an usage example of <code>dl</code> on MacOS and Windows.</p>

## ❡ Install

### Dependencies

The following dependencies must be installed before it:

#### Dependencies For Windows

- [**Visual Studio 2022**](https://visualstudio.microsoft.com): it provides all the tools required to build this library.
- [**git**](https://git-scm.com): it will be used to clone this repository.
- **A font patched by the [Nerd Fonts project](https://www.nerdfonts.com/font-downloads)**: it provides the pretty symbols used by the software.

#### Dependencies For Linux

- **gcc**, **cmake**: they will be used to build this library.
- **git**: it will be used to clone this repository.
- **A font patched by the Nerd Fonts project**: it provides the pretty symbols used by the software.

> [!TIP]
> Use your distro package manager to install these packages.

#### Dependencies For MacOS

- **Apple Command Line Tools**, **cmake**: they will be used to build this library.
- **git**: it will be used to clone this repository.
- **A font patched by the Nerd Fonts project**: it provides the pretty symbols used by the software.

> [!TIP]
> Use `xcode-select --install` to install the Apple command line tools. For the rest, use [HomeBrew](https://brew.sh/).

### Procedures

On Windows, using the `Developer PowerShell for VS 2022` profile or, on any other operating systems, using any terminal, follow these instructions:

- Clone this repository using `git`:

```zsh
git clone --depth 1 --recurse-submodules https://github.com/skippyr/dl
```

- Access its directory:

```zsh
cd dl
```

- Use `cmake` to build and install it:

```zsh
cmake -B build
cmake --build build
cmake --install build
```

- Add the directory `bin` to your system `PATH` environment variable.
- Reload your shell session.
- `dl` should now be installed and available as a command.

## ❡ Documentation

After installed, you can read its help page:

```zsh
dl --help
```

## ❡ Help

If you need help related to this project, open a new issue in its [issues pages](https://github.com/skippyr/dl/issues) or send me an [e-mail](mailto:skippyr.developer@icloud.com) describing what is going on.

## ❡ Contributing

This project is open to review and possibly accept contributions, specially fixes and suggestions. If you are interested, send your contribution to its [pull requests page](https://github.com/skippyr/dl/pulls) or to my [e-mail](mailto:skippyr.developer@icloud.com).

By contributing to this project, you agree to license your work under the same license that the project uses.

## ❡ License

This is free software licensed under the BSD-3-Clause License that comes WITH NO WARRANTY. Refer to the `LICENSE` file that comes in its source code for license and copyright details.

&ensp;
<p align="center"><sup><strong>≥v≥v&ensp;Here Be Dragons!&ensp;≥v≥</strong><br />Made with love by skippyr <3</sup></p>
