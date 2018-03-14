// Copyright 2018 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Checking of compiler and linker flags.
// We must avoid flags like -fplugin=, which can allow
// arbitrary code execution during the build.
// Do not make changes here without carefully
// considering the implications.
// (That's why the code is isolated in a file named security.go.)
//
// Note that -Wl,foo means split foo on commas and pass to
// the linker, so that -Wl,-foo,bar means pass -foo bar to
// the linker. Similarly -Wa,foo for the assembler and so on.
// If any of these are permitted, the wildcard portion must
// disallow commas.
//
// Note also that GNU binutils accept any argument @foo
// as meaning "read more flags from the file foo", so we must
// guard against any command-line argument beginning with @,
// even things like "-I @foo".
// We use load.SafeArg (which is even more conservative)
// to reject these.
//
// Even worse, gcc -I@foo (one arg) turns into cc1 -I @foo (two args),
// so although gcc doesn't expand the @foo, cc1 will.
// So out of paranoia, we reject @ at the beginning of every
// flag argument that might be split into its own argument.

package work

import (
	"cmd/go/internal/load"
	"fmt"
	"os"
	"regexp"
	"strings"
)

var re = regexp.MustCompile

var validCompilerFlags = []*regexp.Regexp{
	re(`-D([A-Za-z_].*)`),
	re(`-I([^@\-].*)`),
	re(`-O`),
	re(`-O([^@\-].*)`),
	re(`-W`),
	re(`-W([^@,]+)`), // -Wall but not -Wa,-foo.
	re(`-f(no-)?blocks`),
	re(`-f(no-)?common`),
	re(`-f(no-)?constant-cfstrings`),
	re(`-f(no-)?exceptions`),
	re(`-finput-charset=([^@\-].*)`),
	re(`-f(no-)?lto`),
	re(`-f(no-)?modules`),
	re(`-f(no-)?objc-arc`),
	re(`-f(no-)?omit-frame-pointer`),
	re(`-f(no-)?openmp(-simd)?`),
	re(`-f(no-)?permissive`),
	re(`-f(no-)?(pic|PIC|pie|PIE)`),
	re(`-f(no-)?rtti`),
	re(`-f(no-)?split-stack`),
	re(`-f(no-)?stack-(.+)`),
	re(`-f(no-)?strict-aliasing`),
	re(`-fsanitize=(.+)`),
	re(`-g([^@\-].*)?`),
	re(`-m(arch|cpu|fpu|tune)=([^@\-].*)`),
	re(`-m(no-)?avx[0-9a-z.]*`),
	re(`-m(no-)?ms-bitfields`),
	re(`-m(no-)?stack-(.+)`),
	re(`-mmacosx-(.+)`),
	re(`-mnop-fun-dllimport`),
	re(`-m(no-)?sse[0-9.]*`),
	re(`-pedantic(-errors)?`),
	re(`-pipe`),
	re(`-pthread`),
	re(`-?-std=([^@\-].*)`),
	re(`-x([^@\-].*)`),
}

var validCompilerFlagsWithNextArg = []string{
	"-D",
	"-I",
	"-isystem",
	"-framework",
	"-x",
}

var validLinkerFlags = []*regexp.Regexp{
	re(`-F([^@\-].*)`),
	re(`-l([^@\-].*)`),
	re(`-L([^@\-].*)`),
	re(`-f(no-)?(pic|PIC|pie|PIE)`),
	re(`-fsanitize=([^@\-].*)`),
	re(`-g([^@\-].*)?`),
	re(`-m(arch|cpu|fpu|tune)=([^@\-].*)`),
	re(`-(pic|PIC|pie|PIE)`),
	re(`-pthread`),
	re(`-?-static([-a-z0-9+]*)`),

	// Note that any wildcards in -Wl need to exclude comma,
	// since -Wl splits its argument at commas and passes
	// them all to the linker uninterpreted. Allowing comma
	// in a wildcard would allow tunnelling arbitrary additional
	// linker arguments through one of these.
	re(`-Wl,--(no-)?as-needed`),
	re(`-Wl,-Bdynamic`),
	re(`-Wl,-Bstatic`),
	re(`-Wl,--disable-new-dtags`),
	re(`-Wl,--enable-new-dtags`),
	re(`-Wl,--end-group`),
	re(`-Wl,-framework,[^,@\-][^,]+`),
	re(`-Wl,-headerpad_max_install_names`),
	re(`-Wl,--no-undefined`),
	re(`-Wl,-rpath,([^,@\-][^,]+)`),
	re(`-Wl,-search_paths_first`),
	re(`-Wl,--start-group`),
	re(`-Wl,-?-unresolved-symbols=[^,]+`),
	re(`-Wl,--(no-)?warn-([^,]+)`),

	re(`[a-zA-Z0-9_/].*\.(a|o|obj|dll|dylib|so)`), // direct linker inputs: x.o or libfoo.so (but not -foo.o or @foo.o)
}

var validLinkerFlagsWithNextArg = []string{
	"-F",
	"-l",
	"-L",
	"-framework",
	"-Wl,-framework",
}

func checkCompilerFlags(name, source string, list []string) error {
	return checkFlags(name, source, list, validCompilerFlags, validCompilerFlagsWithNextArg)
}

func checkLinkerFlags(name, source string, list []string) error {
	return checkFlags(name, source, list, validLinkerFlags, validLinkerFlagsWithNextArg)
}

func checkFlags(name, source string, list []string, valid []*regexp.Regexp, validNext []string) error {
	// Let users override rules with $CGO_CFLAGS_ALLOW, $CGO_CFLAGS_DISALLOW, etc.
	var (
		allow    *regexp.Regexp
		disallow *regexp.Regexp
	)
	if env := os.Getenv("CGO_" + name + "_ALLOW"); env != "" {
		r, err := regexp.Compile(env)
		if err != nil {
			return fmt.Errorf("parsing $CGO_%s_ALLOW: %v", name, err)
		}
		allow = r
	}
	if env := os.Getenv("CGO_" + name + "_DISALLOW"); env != "" {
		r, err := regexp.Compile(env)
		if err != nil {
			return fmt.Errorf("parsing $CGO_%s_DISALLOW: %v", name, err)
		}
		disallow = r
	}

Args:
	for i := 0; i < len(list); i++ {
		arg := list[i]
		if disallow != nil && disallow.FindString(arg) == arg {
			goto Bad
		}
		if allow != nil && allow.FindString(arg) == arg {
			continue Args
		}
		for _, re := range valid {
			if re.FindString(arg) == arg { // must be complete match
				continue Args
			}
		}
		for _, x := range validNext {
			if arg == x {
				if i+1 < len(list) && load.SafeArg(list[i+1]) {
					i++
					continue Args
				}

				// Permit -Wl,-framework -Wl,name.
				if i+1 < len(list) &&
					strings.HasPrefix(arg, "-Wl,") &&
					strings.HasPrefix(list[i+1], "-Wl,") &&
					load.SafeArg(list[i+1][4:]) &&
					!strings.Contains(list[i+1][4:], ",") {
					i++
					continue Args
				}

				if i+1 < len(list) {
					return fmt.Errorf("invalid flag in %s: %s %s (see https://golang.org/s/invalidflag)", source, arg, list[i+1])
				}
				return fmt.Errorf("invalid flag in %s: %s without argument (see https://golang.org/s/invalidflag)", source, arg)
			}
		}
	Bad:
		return fmt.Errorf("invalid flag in %s: %s", source, arg)
	}
	return nil
}
