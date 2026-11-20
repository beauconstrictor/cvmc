# CVMC - CVM Compiler

`cvmc` is a compiler for the CVM (Compact Virtual Machine) programming language, designed to produce extremely small, cross-platform binaries targeting the Compact Virtual Machine. The CVM is a minimal bytecode VM that can quickly be implemented on custom, limited hardware, immediately providing an expressive, high-level programming language for that machine.

Here is CVM's 'Hello, world!' program:

```lisp
import std

{:main
  (puts "Hello, world!")
}
```

The design of the language closely mimics the design of the virtual machine itself, making binary size easy to predict and minimise.

There is a reference manual in the works that will provide more thorough details on programming in the language itself.

## Installation

To install `cvmc`, you will need to install `clang`,  `make` and `xxd`. Then, just run these commands to build a binary which you can move into your PATH:

```sh
$ git clone https://github.com/beauconstrictor/cvmc
$ cd cvmc/
$ make
```

The binary will be located in `cvmc/build/cvmc`. Once you have moved the binary into your PATH, verify that it is installed correctly:

```sh
$ cvmc -v
```

If you use Vim, there is also a syntax highlighter included in this repo, called `cvm.vim`.

## The Language

The CVM language uses a compact syntax with only a few constructions which compose naturally, namely:

- Cheap anonymous functions
- Opti-in prefix notation
- Constants
- Statically-allocated variables
- Enumerations

Most of these features are _syntactic sugar_, meaning they are handled during the compiler's extensive preprocessing phase, where they are removed one-by-one, until the resulting code resembles the VM's bytecode almost exactly. You can inspect the output of the preprocessor at any one of it's many stages, to see how your code is reduced. For more information, see the CLI section.

### Cheap Anonymous Functions

One of the most foundational features of CVM is that all control flow is expressed in terms of functions. This means that `if-else`, for example, is really a function that takes two functions and a condition and executes exactly one of those functions, depending on the value of the condition. In order to make leveraging this less cumbersome, anonymous functions can easily be defined with very little syntax: they are literally just braces around code.

This means you can do conditionals like this:

```lisp
true
{(puts "true is true")}
{(puts "false is true")}
if-else
```

However, you will generally combine anonymous functions with the parenthetical syntax to make this read more naturally:

```lisp
(if-else (true) {
  (puts "true is true")
} {
  (puts "false is true")
})
```

These anonymous functions are referred to as 'cheap' because they take up very little binary size (the same size as the code would be when inlined) and have very little calling overhead, because the language doesn't allocate stack frames for each function call. In effect, anonymous functions have no more cost than any traditional form of control flow, but they allow the VM to understand structured code directly, and make higher-order programming incredibly easy, even in an otherwise extremely low-level language.

Wherever an anonymous function is encountered, it is replaced with a push to the address of that function in the binary.

### Opt-in Prefix Notation

CVM is fundamentally a stack-based, postfix language like Forth or Factor. However, it uses a special, optioanl, syntax to make certain expressions read in a more natural order: when a series of words (functions, numbers, variables, etc.) is surrounded by parentheses, the first word is moved to the end. This means that you can write things like:

```lisp
(add 3 (mul 5 3))
```

And get the simplified but less structured:

```lisp
3 5 3 mul add
```

Keep in mind that this syntax is not required, and either of the above is valid. Technically, you can even write something resembling infix notation, like this:

```lisp
5 (add 3)
```

Certain things are better left without parentheses, such as sequences of stack operations. In this case, the are simply executed from left to right.

### Constants

You can also define compile-time constants, like this:

```js
const x = 5
```

Then, anyhwere you use the word `x`, it will be substituted with `5` instead.

### Statically-allocated Variables

Sometimes, you want to be able to change values at runtime. To achieve this, you use variables instead of constants.

You can create a variable like this:

```js
var y[5]: 2
```

The compiler keeps an internal pointer and, whenever a variable is declared like this, it's value becomes the current value of the pointer, which is then increased by the product of the two numbers (in this case, 5 * 2 = 10). On its own, this isn't particularly useful, but these values can be interpreted as memory addresses, allowing you to read and write to these addresses, while the variable system handled the nitty-gritty of picking addresses:

```lisp
(put (add (get y) 5) y)
```

This example will add 5 to the value at y, by first reading the value in y, adding 5 to it and then putting the result back in y.

In CVM, constants are first substituted, and then variable declarations are handled. This means that you can define constants for common variables sizes, making your code more readable:

```js
const char = 1
const int = 2

var buf[256]: char
var num: int
```

This is how you will usually use variables, with the second size coming from a type, and the first to optionally allocated multiple of that type in a row, creating an array.

Unlike constants and enums, variables are _scoped_, meaning you can create a variable inside a function and it will only be accessible inside that function, allowing you to reuse common variables names. However, these are not truly local variables, so they will be overwritten in recursive functions - either avoid recursion entirely, or use the stack to store temporary data entirely, like this:

```lisp
{:foo
  var x: int
  (put 10 x)
}

{:bar
  var x: int
  (put 20 x)
}
```

### Enumerations

Enumerations allow you to define groups of values with convenient names. For example:

```lisp
enum species: (human, cow, sheep, pig, goat)
```

This line will create a series of constants for you, `species.human`, `species.cow`, etc. with unique ascending values, starting from 0.

An enum will also create a constant with a value of two for you, allowing you to conveniently use enums as types for variables (see above). This constant will be the name of the enum, suffixed with a `-t` (for type):

```
var my-species: species-t

{:main
    (put species.human my-species)
}
```

You can even add more options to an enum after you create it, and they will continue to be assigned ascending numbers, starting from where the previous definition left off. This is most commonly used to create custom error types, which are all enums of `errno`. `errno` is an enum defined in the `std` module. The module also defines a variables called `errno`, and this is the system used to report errors by functions all standard library modules (and your own, if you like). Functions that return an error simply set the value of `errno` to an enum option with a name representing what went wrong. It is your responsibility to set `errno` to `errno.success` before calling a function that you want to check for errors.

### String Literals

Even string literals in CVM are syntactic sugar: they expand to an anonymous function containing the bytes of the string, and a null-terminator.

## CLI

`cvmc`'s CLI is very simple. To see a list of available options, run:

```sh
$ cvmc -h
```

These are the preprocessor stages that you can inspect:

1. Import statements are expanded, producing a single token stream
2. Enumerations are read and subtituted
3. Parenthesised expressions are converted to postfix
4. String literals are converted to anonymous blocks
5. Local variables are converted to namespaced globals
6. Variables and constants are allocated and substituted
7. Anonymous blocks are extracted into global functions
