// AST

#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>
#include <inttypes.h>

#define IDENTLEN 10

typedef int64_t int64;
typedef int64   values;
typedef char    alfa[IDENTLEN];

// language symbols
typedef enum { word, numeral, empty, nilsy, literal, truesy, falsesy, opensy, 
               closesy, sqopen, sqclose, curlopen, curlclose, letsy, recsy, insy, 
               comma, colon, ifsy, thensy, elsesy, lambdasy, dot, quote, 
               chansy, sequencesy, parallelsy, choicesy, inputsy, outputsy, conssy, orsy, 
               andsy, eq, ne, lt, le, gt, ge, plus,
               minus, times, over, nullsy, hdsy, tlsy, notsy, spawnsy, unknown, eofsy } symbol;

// language keywords
typedef enum { ident, intcon, boolcon, charcon, emptycon, nilcon,
               newchan, lambdaexp, application, unexp, binexp, ifexp, block,
               declist, decln } syntaxclass;

// bytecode cells
typedef struct node* tree;
typedef struct node { 
    syntaxclass tag;
    union {
        alfa id; // string
        int  n;  // int
        int  b;  // bool
        char ch; // char
        struct { tree parm, body; } lambda;
        struct { tree func, parm; } application;
        struct { tree decs, expr; } block;
        struct { tree left, right; symbol op; } expression;
        struct { alfa name; tree value; } decln;
        struct { tree head, tail; symbol recursive; } declist;
        struct { tree e1, e2, e3; } ifexp; // not needed
    };
} node;

// parser 
typedef struct parser_ctx {
    FILE   *stream;
    int    lineno;
    char   ch;
    symbol sy;
    alfa   theword;
    int    theint;
    int    oprpriority[eofsy];
    int64  startsexp;
    int64  unoprs;
    int64  binoprs;
    int64  rightassoc;
} parser_ctx;

void parser_init(parser_ctx *context);
tree parse(parser_ctx *context);
void print(int deep, tree t);
tree newnode(syntaxclass c);

#endif
