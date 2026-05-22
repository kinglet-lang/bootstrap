module.exports = grammar({
  name: 'kinglet',

  extras: $ => [
    $.comment,
    /[\s\n]/,
  ],

  rules: {
    source_file: $ => repeat(choice(
      $.using_declaration,
      $.function_declaration,
    )),

    // ── using / namespace ──
    using_declaration: $ => seq(
      'using',
      optional('namespace'),
      $.identifier,
      ';',
    ),

    // ── Functions ──
    function_declaration: $ => seq(
      field('return_type', $.type),
      field('name', $.identifier),
      field('parameters', $.parameters),
      field('body', choice($.block, $.expression_body)),
    ),

    parameters: $ => seq(
      '(',
      optional(seq(
        $.parameter,
        repeat(seq(',', $.parameter)),
        optional(','),
      )),
      ')',
    ),

    parameter: $ => seq(
      field('type', $.type),
      field('name', $.identifier),
    ),

    expression_body: $ => seq('=>', $._expression, ';'),

    // ── Statements ──
    block: $ => seq('{', repeat($._statement), '}'),

    _statement: $ => choice(
      $.block,
      $.return_statement,
      $.variable_declaration,
      $.if_statement,
      $.while_statement,
      $.for_statement,
      $.break_statement,
      $.continue_statement,
      $.expression_statement,
    ),

    return_statement: $ => seq('return', optional($._expression), ';'),

    expression_statement: $ => seq($._expression, ';'),

    break_statement: $ => seq('break', ';'),

    continue_statement: $ => seq('continue', ';'),

    // ── Variables ──
    variable_declaration: $ => seq(
      optional('const'),
      field('type', $.type),
      field('name', $.identifier),
      optional(seq('=', $._expression)),
      ';',
    ),

    // ── Control Flow ──
    if_statement: $ => seq(
      'if',
      '(',
      field('condition', $._expression),
      ')',
      field('consequence', $._statement),
      optional(seq('else', field('alternative', $._statement))),
    ),

    while_statement: $ => seq(
      'while',
      '(',
      field('condition', $._expression),
      ')',
      field('body', $._statement),
    ),

    for_statement: $ => seq(
      'for',
      '(',
      field('initializer', optional($._statement)),
      field('condition', optional($._expression)),
      ';',
      field('increment', optional($._expression)),
      ')',
      field('body', $._statement),
    ),

    // ── Inspect (Pattern Matching) ──
    inspect_expression: $ => seq(
      'inspect',
      '(',
      field('value', $._expression),
      ')',
      '{',
      repeat($.inspect_arm),
      '}',
    ),

    inspect_arm: $ => seq(
      field('pattern', $._expression),
      '=>',
      field('body', $._expression),
      optional(','),
    ),

    // ── Expressions ──
    _expression: $ => choice(
      $.assignment_expression,
      $.binary_expression,
      $.unary_expression,
      $.call_expression,
      $.namespace_access,
      $.inspect_expression,
      $.identifier,
      $.number,
      $.float,
      $.string,
      $.char,
      $.true,
      $.false,
      $.null,
      $.parenthesized_expression,
    ),

    assignment_expression: $ => prec.right(1, seq(
      field('left', $.identifier),
      field('operator', choice('=', '+=', '-=', '*=', '/=')),
      field('right', $._expression),
    )),

    binary_expression: $ => {
      const table = [
        ['||'],
        ['&&'],
        ['==', '!='],
        ['<', '>', '<=', '>='],
        ['+', '-'],
        ['*', '/', '%'],
      ];

      return choice(...table.map((operators) =>
        prec.left(1, seq(
          field('left', $._expression),
          field('operator', choice(...operators)),
          field('right', $._expression),
        ))
      ));
    },

    unary_expression: $ => prec.left(2, seq(
      field('operator', choice('-', '!', '~')),
      field('argument', $._expression),
    )),

    call_expression: $ => prec(3, seq(
      field('function', choice($.identifier, $.namespace_access)),
      field('arguments', $.arguments),
    )),

    arguments: $ => seq('(', optional(seq(
      $._expression,
      repeat(seq(',', $._expression)),
      optional(','),
    )), ')'),

    namespace_access: $ => seq(
      field('namespace', $.identifier),
      '::',
      field('member', $.identifier),
    ),

    parenthesized_expression: $ => seq('(', $._expression, ')'),

    // ── Primitives ──
    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,

    number: $ => token(prec(1, choice(
      /0[xX][0-9a-fA-F_]+/,
      /0[bB][01_]+/,
      /[0-9][0-9_]*/,
    ))),

    float: $ => token(choice(
      /[0-9][0-9_]*\.[0-9][0-9_]*/,
      /[0-9][0-9_]*[eE][+-]?[0-9][0-9_]*/,
    )),

    string: $ => token(seq(
      '"',
      repeat(choice(
        /[^"\\]/,
        /\\./,
      )),
      '"',
    )),

    char: $ => token(seq(
      "'",
      choice(/[^'\\]/, /\\./),
      "'",
    )),

    true: $ => 'true',
    false: $ => 'false',
    null: $ => 'null',

    comment: $ => token(choice(
      seq('//', /.*/),
      seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/'),
    )),

    // ── Types ──
    type: $ => choice(
      'int',
      'float',
      'double',
      'bool',
      'string',
      'void',
      'byte',
      'auto',
    ),
  },
});
