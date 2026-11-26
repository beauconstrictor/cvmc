syntax clear

setlocal iskeyword+=-

syntax keyword cvm_loop
      \ while do-while forever

syntax keyword cvm_if
      \ if if-else match cases

syntax keyword cvm_keyword
      \ size len
      \ var const enum
      \ import
      \ get put load store at clear
      \ do doc
      \ break throw assert
      \ errno

syntax keyword cvm_builtin
      \ call
      \ neg abs sgn set-sgn
      \ add sub inc dec mul div udiv mod umod pow
      \ shl shr b-not b-and b-nand b-or b-nor b-xor
      \ gt lt ge le eq ne ucmp
      \ not and nand or nor xor bool-of
      \ dup drop swap over rot nip tuck
      \ clear-errno check-errno
      \ putc puti puti putu puts putl putb
      \ gets getc geti
      \ slen send scpy scmp
      \ isdigit islower isupper isalpha isalnum isspace

syntax keyword cvm_type
      \ char byte bool i8 u8
      \ int uint i16 u16 ptr callable str errno-t

syntax keyword cvm_internal
      \ udiv-mod div-mod bitwise

highlight link cvm_loop Repeat
highlight link cvm_if Conditional
highlight link cvm_keyword Keyword
highlight link cvm_builtin Function
highlight link cvm_internal Error

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
