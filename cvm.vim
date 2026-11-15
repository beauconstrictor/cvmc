syntax clear

setlocal iskeyword+=-

syntax keyword cvm_loop
      \ while do-while

syntax keyword cvm_if
      \ if if-else

syntax keyword cvm_keyword
      \ var const
      \ import
      \ get set load store

syntax keyword cvm_builtin
      \ i
      \ neg abs sgn set-sgn
      \ add sub inc dec mul div mod
      \ gt lt ge le eq ne
      \ not and nand or nor bool-of
      \ dup drop swap over rot
      \ putc puti puts putl putb

syntax keyword cvm_type
      \ int uint i16 u16
      \ char byte bool i8 u8

highlight link cvm_loop Repeat
highlight link cvm_if Conditional
highlight link cvm_keyword Keyword
highlight link cvm_builtin Function

syntax region cvm_comment start=/</ end=/>/ contains=cvm_comment
highlight link cvm_comment Comment

syntax region cvm_str_literal start=/"/ skip=/\\"/ end=/"/
highlight link cvm_str_literal String

syntax match cvm_int_literal "\<-\?\d\+\>"
highlight link cvm_int_literal Number

syntax region cvm_char_literal start=+'+ skip=+\\.+ end=+'+
highlight link cvm_char_literal Character

syntax match cvm_definition /{:\zs[a-zA-Z0-9_.-]\+/
highlight link cvm_definition Define

syntax keyword cvm_bool true false
highlight link cvm_bool Boolean

highlight link cvm_type Type
