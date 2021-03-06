#include <string.h>
#include <stddef.h>

#include "cii/list.h"
#include "cii/except.h"
#include "cii/mem.h"
#include "cii/assert.h"

#include "ast.h"
#include "token.h"


typedef ParseResult (*ParseFunc_T) (List_T);
typedef void (*VerifyFunc_T) (ExprAst);

static void assert_var(ExprAst expr_ast);
static List_T wait_token(List_T Tokens, const char *t);
static void handle_num(void* first, List_T Tokens, ParseResult pr);
static void handle_var(void *first, List_T Tokens, ParseResult pr);
static void handle_bin(void *first, List_T Tokens, ParseResult pr);
static void handle_call(void *first, List_T Tokens, ParseResult pr);
static void handle_defun(void *first, List_T Tokens, ParseResult pr);
static void handle_prototype(void *first, List_T Tokens, ParseResult pr);
static void handle_cmp(void *first, List_T Tokens, ParseResult pr);
static void handle_if(void *first, List_T Tokens, ParseResult pr);
static void handle_extern_dec(void *first, List_T Tokens, ParseResult pr);
static ParseResult parse_multi(ParseFunc_T parse_func ,
                               List_T Tokens, const char *delim,
                               VerifyFunc_T verify_func);
static ParseResult parse_exp(List_T Tokens);

Except_T Parse_Failed = { "Parse Failed" };
Except_T Parse_Multi_Assert_Var_Fail = { "Parse_Multi Assert Var Fail" };

List_T
parse(List_T Tokens)
{
    List_T es = NULL;
    while (Tokens)
    {
        ParseResult pr = parse_exp(Tokens);
        es = List_push(es, pr->expr_ast);
        Tokens = pr->Rem_tokens;
    }

    return List_reverse(es);
}


static ParseResult
parse_exp(List_T Tokens)
{
    ParseResult pr;
    
    if (List_length(Tokens) == 0)
    {
        NEW0(pr);
        return pr;
    }

    NEW(pr);
    
    switch(TOK_TAG(Tokens->first))
    {
    case NumToken_T:
        handle_num(Tokens->first, Tokens->rest, pr);
        break;
    case VarToken_T:
        handle_var(Tokens->first, Tokens->rest, pr);
        break;
    case OpToken_T:
        handle_bin(Tokens->first, Tokens->rest, pr);
        break;
    case LBraceToken_T:
        handle_call(Tokens->first, Tokens->rest, pr);
        break;
    case DefunToken_T:
        handle_defun(Tokens->first, Tokens->rest, pr);
        break;
    case LSqBraceToken_T:
        handle_cmp(Tokens->first, Tokens->rest, pr);
        break;
    case IfToken_T:
        handle_if(Tokens->first, Tokens->rest, pr);
        break;
    case ExternToken_T:
        handle_extern_dec(Tokens->first, Tokens->rest, pr);
        break;
    default:
        RAISE(Parse_Failed);
    }
    
    return pr;
}

static List_T
wait_token(List_T Tokens, const char *t)
{
    if (t == NULL)
        return Tokens;
    
    assert(Tokens);
    
    if (strcmp(TOK_TEXT(Tokens->first), t) != 0)
        RAISE(Parse_Failed);
    
    return Tokens->rest;
}

static void
handle_num(void* first, List_T Tokens, ParseResult pr)
{
    NumExprAst num_expr;
    NumToken num_token = first;

    assert(IS_TOKEN(num_token, NumToken));
    
    NEW(num_expr);
    num_expr->tag = NumExprAst_T;
    num_expr->val = num_token->num;

    pr->expr_ast = (ExprAst)num_expr;
    pr->Rem_tokens = Tokens;
}

static void
handle_var(void *first, List_T Tokens, ParseResult pr)
{
    VarExprAst var_expr;
    VarToken var_token = first;

    assert(IS_TOKEN(var_token, VarToken));
    
    NEW(var_expr);
    var_expr->tag = VarExprAst_T;
    var_expr->name = var_token->raw_text;
    
    pr->expr_ast = (ExprAst)var_expr;
    pr->Rem_tokens = Tokens;
}

static void
handle_bin(void *first, List_T Tokens, ParseResult pr)
{
    BinExprAst bin_expr;
    OpToken op_token = first;

    assert(IS_TOKEN(op_token, OpToken));

    NEW(bin_expr);
    bin_expr->tag = BinExprAst_T;
    bin_expr->op = op_token->raw_text;

    List_T R1 = wait_token(Tokens, "(");
    ParseResult pr1 = parse_exp(R1);
    List_T R2 = wait_token(pr1->Rem_tokens, ",");
    ParseResult pr2 = parse_exp(R2);
    List_T R3 = wait_token(pr2->Rem_tokens, ")");
    
    bin_expr->LHS = pr1->expr_ast;
    bin_expr->RHS = pr2->expr_ast;

    pr->expr_ast = (ExprAst)bin_expr;
    pr->Rem_tokens = R3;
}

static void
handle_call(void *first, List_T Tokens, ParseResult pr)
{
    CallExprAst call_expr;
    LBracketToken l_bracket_token = first;

    assert(IS_TOKEN(first, LBraceToken));

    NEW(call_expr);
    call_expr->tag = CallExprAst_T;
    ParseResult pr1 = parse_exp(Tokens);
    assert(IsA(pr1->expr_ast, VarExprAst));
    List_T R0 = wait_token(pr1->Rem_tokens, ",");
    ParseResult pr2 = parse_multi(parse_exp, R0, ",", NULL);
    List_T R1 = wait_token(pr2->Rem_tokens, "}");
    call_expr->callee = ((VarExprAst)(pr1->expr_ast))->name;
    call_expr->args = pr2->list_result;

    pr->expr_ast = (ExprAst)call_expr;
    pr->Rem_tokens = R1;
}

static void
handle_defun(void *first, List_T Tokens, ParseResult pr)
{
    FunctionAst func_expr;

    DefunToken defun_token = first;
    assert(IS_TOKEN(defun_token, DefunToken));

    NEW(func_expr);
    func_expr->tag = FunctionAst_T;
    
    ParseResult pr1;
    NEW0(pr1);
    handle_prototype(first, Tokens, pr1);

    ParseResult pr2;
    pr2 = parse_exp(pr1->Rem_tokens);

    func_expr->proto = (PrototypeAst)(pr1->expr_ast);
    func_expr->body = (ExprAst)(pr2->expr_ast);
    
    pr->expr_ast = (ExprAst)func_expr;
    pr->Rem_tokens = pr2->Rem_tokens;
}

static void
handle_prototype(void *first, List_T Tokens, ParseResult pr)
{
    PrototypeAst prototype;

    DefunToken defun_token = first;
    assert(IS_TOKEN(defun_token, DefunToken));
    
    NEW(prototype);
    prototype->tag = PrototypeAst_T;
    
    ParseResult pr1;
    NEW0(pr1);
    
    assert(Tokens);
    handle_var(Tokens->first, Tokens->rest, pr1);

    VarExprAst var = (VarExprAst)(pr1->expr_ast);
    
    List_T R1 = wait_token(pr1->Rem_tokens, "(");
    ParseResult pr2 = parse_multi(parse_exp, R1, ",", assert_var);
    List_T R2 = wait_token(pr2->Rem_tokens, ")");

    prototype->function_name = var->name;
    prototype->args = pr2->list_result;

    pr->expr_ast = (ExprAst)prototype;
    pr->Rem_tokens = R2;
}

static void
handle_cmp(void *first, List_T Tokens, ParseResult pr)
{
    CmpExprAst cmp_expr;

    LSqBraceToken lsq_brace_token = first;
    assert(IS_TOKEN(lsq_brace_token, LSqBraceToken));

    NEW(cmp_expr);
    cmp_expr->tag = CmpExprAst_T;

    assert(Tokens->first);
    OpToken op_token = (OpToken)(Tokens->first);
    assert(IS_TOKEN(op_token, OpToken));
    cmp_expr->op = op_token->raw_text;

    List_T R1 = wait_token(Tokens->rest, ",");

    ParseResult pr1;
    pr1 = parse_exp(R1);
    cmp_expr->C1 = pr1->expr_ast;

    List_T R2 = wait_token(pr1->Rem_tokens, ",");
    ParseResult pr2;
    pr2 = parse_exp(R2);
    cmp_expr->C2 = pr2->expr_ast;

    List_T R3 = wait_token(pr2->Rem_tokens, "]");

    pr->expr_ast = (ExprAst)cmp_expr;
    pr->Rem_tokens = R3;
}

static void
handle_if(void *first, List_T Tokens, ParseResult pr)
{
    IfExprAst if_expr;

    IfToken if_token = first;
    assert(IS_TOKEN(if_token, IfToken));

    NEW0(if_expr);
    if_expr->tag = IfExprAst_T;

    ParseResult pr1;
    pr1 = parse_exp(Tokens);

    ExprAst cmp = pr1->expr_ast;
    if_expr->cmp = (CmpExprAst)cmp;

    List_T R1 = wait_token(pr1->Rem_tokens, "then");
    
    ParseResult pr2;
    pr2 = parse_exp(R1);
    if_expr->then_body = pr2->expr_ast;

    /* test have else */
    int has_else = 0;
    
    if (pr2->Rem_tokens)
    {
        Token t = (Token)(pr2->Rem_tokens->first);
        if (IS_TOKEN(t, ElseToken))
            has_else = 1;
    }

    if (has_else)
    {
        List_T R2 = wait_token(pr2->Rem_tokens, "else");
        ParseResult pr3;
        
        pr3 = parse_exp(R2);
        if_expr->else_body = pr3->expr_ast;

        pr->expr_ast = (ExprAst)if_expr;
        pr->Rem_tokens = pr3->Rem_tokens;
    }
    else
    {
        pr->expr_ast = (ExprAst)if_expr;
        pr->Rem_tokens = pr2->Rem_tokens;
    }
}

static void
handle_extern_dec(void *first, List_T Tokens, ParseResult pr)
{
    DefunToken defun_token;
    NEW(defun_token);
    defun_token->tag = DefunToken_T;
    
    ParseResult pr1;
    NEW0(pr1);
    handle_prototype((void *)defun_token, Tokens, pr1);

    List_T R = wait_token(pr1->Rem_tokens, ";");
    
    pr->expr_ast = pr1->expr_ast;
    pr->Rem_tokens = R;
}

static ParseResult
parse_multi(ParseFunc_T parse_func , List_T Tokens,
            const char *delim, VerifyFunc_T verify_func)
{
    volatile ParseResult pr;
    volatile List_T list_result = NULL;
    volatile List_T Rem_Toks = Tokens;
    volatile Except_T e;
    
    for(;;)
    {
        TRY
        {
            pr = parse_func(Rem_Toks);
            if (verify_func)
                verify_func(pr->expr_ast);
            list_result = List_push(list_result, pr->expr_ast);
        }
        EXCEPT(Parse_Failed)
        {
            ParseResult pr1;
            NEW0(pr1);
            pr1->list_result = list_result;
            pr1->Rem_tokens = Rem_Toks;
            return pr1;
        }
        END_TRY;

        TRY
        {
            Rem_Toks = wait_token(pr->Rem_tokens, delim);
        }
        EXCEPT(Parse_Failed)
        {
            ParseResult pr2;
            NEW0(pr2);
            list_result = List_reverse(list_result);
            pr2->list_result = list_result;
            pr2->Rem_tokens = pr->Rem_tokens;
            return pr2;
        }
        END_TRY;
    }
}

static void
assert_var(ExprAst expr_ast)
{
    if (!IsA(expr_ast, VarExprAst))
        RAISE(Parse_Multi_Assert_Var_Fail);
}
