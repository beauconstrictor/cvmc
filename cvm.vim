syntax clear

setlocal iskeyword+=-

syntax keyword cvm_loop
      \ while do-while

syntax keyword cvm_if
      \ if if-else switch case

syntax keyword cvm_keyword
      \ size len
      \ var const enum
      \ import
      \ get put load store at clear
      \ break
      \ errno

syntax keyword cvm_builtin
      \ call
      \ neg abs sgn set-sgn
      \ add sub inc dec mul div udiv mod umod pow
      \ gt lt ge le eq ne ucmp
      \ not and nand or nor bool-of
      \ dup drop swap over rot
      \ putc puti puts putl putb
      \ gets getc geti
      \ slen send scpy

syntax keyword cvm_type
      \ char byte bool i8 u8
      \ int uint i16 u16 ptr callable str errno-t

highlight link cvm_loop Repeat
highlight link cvm_if Conditional
highlight link cvm_keyword Keyword
highlight link cvm_builtin Function

syntax match cvm_comment_oneline "--.*"
highlight link cvm_comment_oneline Comment
syntax region cvm_comment_multiline start=/</ end=/>/ contains=cvm_comment_multiline
highlight link cvm_comment_multiline Comment

syntax region cvm_str_literal start=/"/ skip=/\\"/ end=/"/ contains=cvm_str_escape
highlight link cvm_str_literal String

syntax match cvm_str_escape "\\." contained
highlight link cvm_str_escape SpecialChar

syntax match cvm_int_literal "\<-\?\d\+\>"
highlight link cvm_int_literal Number

syntax region cvm_char_literal start=+'+ skip=+\\.+ end=+'+ contains=cvm_str_escape
highlight link cvm_char_literal Character

syntax match cvm_definition /{:\zs[a-zA-Z0-9_.-]\+/
highlight link cvm_definition Define

syntax keyword cvm_bool true false
highlight link cvm_bool Boolean

highlight link cvm_type Type
