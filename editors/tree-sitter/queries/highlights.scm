; ── Types ──
(type) @type.builtin

; ── Keywords ──
"if" @keyword.conditional
"else" @keyword.conditional
"while" @keyword.repeat
"for" @keyword.repeat
"break" @keyword.control
"continue" @keyword.control
"return" @keyword.control
"inspect" @keyword.conditional
"using" @keyword.import
"namespace" @keyword
"spawn" @keyword
"select" @keyword
"struct" @keyword.type
"enum" @keyword.type
"trait" @keyword

; ── Storage modifiers ──
"const" @keyword.storage
"import" @keyword.import
"export" @keyword.import

; ── Constants ──
"true" @boolean
"false" @boolean
"null" @constant.builtin

; ── Numbers ──
(number) @number
(float) @number.float

; ── Strings ──
(string) @string
(char) @character

; ── Operators ──
[
  "+" "-" "*" "/" "%"
  "==" "!=" "<" ">" "<=" ">="
  "&&" "||" "!" "~"
  "=" "+=" "-=" "*=" "/="
  "=>" "::"
] @operator

; ── Functions ──
(function_declaration name: (identifier) @function)
(call_expression function: (identifier) @function.call)
(parameter name: (identifier) @variable.parameter)

; ── Namespace ──
(namespace_access namespace: (identifier) @namespace)
(namespace_access member: (identifier) @function.call)

; ── Variables ──
(variable_declaration name: (identifier) @variable)
(assignment_expression left: (identifier) @variable)

; ── Comments ──
(comment) @comment @spell

; ── Punctuation ──
[
  "(" ")" "{" "}" "[" "]"
  ";" "."
] @punctuation.bracket
