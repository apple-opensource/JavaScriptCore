// -*- c-basic-offset: 2 -*-
/*
 *  This file is part of the KDE libraries
 *  Copyright (C) 1999-2000 Harri Porten (porten@kde.org)
 *  Copyright (C) 2001 Peter Kelly (pmk@post.com)
 *  Copyright (C) 2003 Apple Computer, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA 02111-1307, USA.
 *
 */

#ifndef _NODES_H_
#define _NODES_H_

#include "fast_malloc.h"
#include "shared_ptr.h"

#include "internal.h"
//#include "debugger.h"
#ifndef NDEBUG
#ifndef __osf__
#include <list>
#endif
#endif

namespace KJS {

  class ProgramNode;
  class PropertyNode;
  class PropertyValueNode;
  class Reference;
  class RegExp;
  class SourceElementsNode;
  class SourceStream;

  enum Operator { OpEqual,
		  OpEqEq,
		  OpNotEq,
		  OpStrEq,
		  OpStrNEq,
		  OpPlusEq,
		  OpMinusEq,
		  OpMultEq,
		  OpDivEq,
                  OpPlusPlus,
		  OpMinusMinus,
		  OpLess,
		  OpLessEq,
		  OpGreater,
		  OpGreaterEq,
		  OpAndEq,
		  OpXOrEq,
		  OpOrEq,
		  OpModEq,
                  OpAnd,
                  OpOr,
		  OpBitAnd,
		  OpBitXOr,
		  OpBitOr,
		  OpLShift,
		  OpRShift,
		  OpURShift,
		  OpIn,
		  OpInstanceOf
  };

  class Node {
  public:
    Node();
    virtual ~Node();

    KJS_FAST_ALLOCATED;

    virtual Value evaluate(ExecState *exec) = 0;
    virtual Reference evaluateReference(ExecState *exec);
    UString toString() const;
    virtual void streamTo(SourceStream &s) const = 0;
    virtual void processVarDecls(ExecState */*exec*/) {}
    int lineNo() const { return line; }

  public:
    // reference counting mechanism
    void ref() { ++m_refcount; }
    void deref() { --m_refcount; if (!m_refcount) delete this; }

  protected:
    Value throwError(ExecState *exec, ErrorType e, const char *msg);
    Value throwError(ExecState *exec, ErrorType e, const char *msg, Value v, Node *expr);
    Value throwError(ExecState *exec, ErrorType e, const char *msg, Identifier label);
    void setExceptionDetailsIfNeeded(ExecState *);
    int line;
    UString sourceURL;
    unsigned int m_refcount;
    virtual int sourceId() const { return -1; }
  private:
    // disallow assignment
    Node& operator=(const Node&);
    Node(const Node &other);
  };

  class StatementNode : public Node {
  public:
    StatementNode();
    void setLoc(int line0, int line1, int sourceId);
    int firstLine() const { return l0; }
    int lastLine() const { return l1; }
    int sourceId() const { return sid; }
    bool hitStatement(ExecState *exec);
    bool abortStatement(ExecState *exec);
    virtual Completion execute(ExecState *exec) = 0;
    void pushLabel(const Identifier &id) { ls.push(id); }
    virtual void processFuncDecl(ExecState *exec);
  protected:
    LabelStack ls;
  private:
    Value evaluate(ExecState */*exec*/) { return Undefined(); }
    int l0, l1;
    int sid;
    bool breakPoint;
  };

  class NullNode : public Node {
  public:
    NullNode() {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  };

  class BooleanNode : public Node {
  public:
    BooleanNode(bool v) : value(v) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    bool value;
  };

  class NumberNode : public Node {
  public:
    NumberNode(double v) : value(v) { }
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    double value;
  };

  class StringNode : public Node {
  public:
    StringNode(const UString *v) { value = *v; }
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    UString value;
  };

  class RegExpNode : public Node {
  public:
    RegExpNode(const UString &p, const UString &f)
      : pattern(p), flags(f) { }
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    UString pattern, flags;
  };

  class ThisNode : public Node {
  public:
    ThisNode() {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  };

  class ResolveNode : public Node {
  public:
    ResolveNode(const Identifier &s) : ident(s) { }
    Value evaluate(ExecState *exec);
    virtual Reference evaluateReference(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    Identifier ident;
  };

  class GroupNode : public Node {
  public:
    GroupNode(Node *g) : group(g) { }
    virtual Value evaluate(ExecState *exec);
    virtual Reference evaluateReference(ExecState *exec);
    virtual void streamTo(SourceStream &s) const { group->streamTo(s); }
  private:
    kxmlcore::SharedPtr<Node> group;
  };

  class ElementNode : public Node {
  public:
    // list pointer is tail of a circular list, cracked in the ArrayNode ctor
    ElementNode(int e, Node *n) : list(this), elision(e), node(n) { }
    ElementNode(ElementNode *l, int e, Node *n)
      : list(l->list), elision(e), node(n) { l->list = this; }
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    friend class ArrayNode;
    kxmlcore::SharedPtr<ElementNode> list;
    int elision;
    kxmlcore::SharedPtr<Node> node;
  };

  class ArrayNode : public Node {
  public:
    ArrayNode(int e) : element(0), elision(e), opt(true) { }
    ArrayNode(ElementNode *ele)
      : element(ele->list), elision(0), opt(false) { ele->list = 0; }
    ArrayNode(int eli, ElementNode *ele)
      : element(ele->list), elision(eli), opt(true) { ele->list = 0; }
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<ElementNode> element;
    int elision;
    bool opt;
  };

  class PropertyValueNode : public Node {
  public:
    // list pointer is tail of a circular list, cracked in the ObjectLiteralNode ctor
    PropertyValueNode(PropertyNode *n, Node *a)
      : name(n), assign(a), list(this) { }
    PropertyValueNode(PropertyNode *n, Node *a, PropertyValueNode *l)
      : name(n), assign(a), list(l->list) { l->list = this; }
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    friend class ObjectLiteralNode;
    kxmlcore::SharedPtr<PropertyNode> name;
    kxmlcore::SharedPtr<Node> assign;
    kxmlcore::SharedPtr<PropertyValueNode> list;
  };

  class ObjectLiteralNode : public Node {
  public:
    ObjectLiteralNode() : list(0) { }
    ObjectLiteralNode(PropertyValueNode *l) : list(l->list) { l->list = 0; }
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<PropertyValueNode> list;
  };

  class PropertyNode : public Node {
  public:
    PropertyNode(double d) : numeric(d) { }
    PropertyNode(const Identifier &s) : str(s) { }
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    double numeric;
    Identifier str;
  };

  class AccessorNode1 : public Node {
  public:
    AccessorNode1(Node *e1, Node *e2) : expr1(e1), expr2(e2) {}
    Value evaluate(ExecState *exec);
    virtual Reference evaluateReference(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr1;
    kxmlcore::SharedPtr<Node> expr2;
  };

  class AccessorNode2 : public Node {
  public:
    AccessorNode2(Node *e, const Identifier &s) : expr(e), ident(s) { }
    Value evaluate(ExecState *exec);
    virtual Reference evaluateReference(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
    Identifier ident;
  };

  class ArgumentListNode : public Node {
  public:
    // list pointer is tail of a circular list, cracked in the ArgumentsNode ctor
    ArgumentListNode(Node *e) : list(this), expr(e) { }
    ArgumentListNode(ArgumentListNode *l, Node *e)
      : list(l->list), expr(e) { l->list = this; }
    Value evaluate(ExecState *exec);
    List evaluateList(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    friend class ArgumentsNode;
    kxmlcore::SharedPtr<ArgumentListNode> list;
    kxmlcore::SharedPtr<Node> expr;
  };

  class ArgumentsNode : public Node {
  public:
    ArgumentsNode() : list(0) { }
    ArgumentsNode(ArgumentListNode *l)
      : list(l->list) { l->list = 0; }
    Value evaluate(ExecState *exec);
    List evaluateList(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<ArgumentListNode> list;
  };

  class NewExprNode : public Node {
  public:
    NewExprNode(Node *e) : expr(e), args(0) {}
    NewExprNode(Node *e, ArgumentsNode *a) : expr(e), args(a) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
    kxmlcore::SharedPtr<ArgumentsNode> args;
  };

  class FunctionCallNode : public Node {
  public:
    FunctionCallNode(Node *e, ArgumentsNode *a) : expr(e), args(a) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
    kxmlcore::SharedPtr<ArgumentsNode> args;
  };

  class PostfixNode : public Node {
  public:
    PostfixNode(Node *e, Operator o) : expr(e), oper(o) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
    Operator oper;
  };

  class DeleteNode : public Node {
  public:
    DeleteNode(Node *e) : expr(e) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    Node *expr;
  };

  class VoidNode : public Node {
  public:
    VoidNode(Node *e) : expr(e) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
  };

  class TypeOfNode : public Node {
  public:
    TypeOfNode(Node *e) : expr(e) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    Node *expr;
  };

  class PrefixNode : public Node {
  public:
    PrefixNode(Operator o, Node *e) : oper(o), expr(e) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    Operator oper;
    kxmlcore::SharedPtr<Node> expr;
  };

  class UnaryPlusNode : public Node {
  public:
    UnaryPlusNode(Node *e) : expr(e) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
  };

  class NegateNode : public Node {
  public:
    NegateNode(Node *e) : expr(e) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
  };

  class BitwiseNotNode : public Node {
  public:
    BitwiseNotNode(Node *e) : expr(e) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
  };

  class LogicalNotNode : public Node {
  public:
    LogicalNotNode(Node *e) : expr(e) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
  };

  class MultNode : public Node {
  public:
    MultNode(Node *t1, Node *t2, char op) : term1(t1), term2(t2), oper(op) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> term1;
    kxmlcore::SharedPtr<Node> term2;
    char oper;
  };

  class AddNode : public Node {
  public:
    AddNode(Node *t1, Node *t2, char op) : term1(t1), term2(t2), oper(op) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> term1;
    kxmlcore::SharedPtr<Node> term2;
    char oper;
  };

  class ShiftNode : public Node {
  public:
    ShiftNode(Node *t1, Operator o, Node *t2)
      : term1(t1), term2(t2), oper(o) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> term1;
    kxmlcore::SharedPtr<Node> term2;
    Operator oper;
  };

  class RelationalNode : public Node {
  public:
    RelationalNode(Node *e1, Operator o, Node *e2) :
      expr1(e1), expr2(e2), oper(o) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr1;
    kxmlcore::SharedPtr<Node> expr2;
    Operator oper;
  };

  class EqualNode : public Node {
  public:
    EqualNode(Node *e1, Operator o, Node *e2)
      : expr1(e1), expr2(e2), oper(o) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr1;
    kxmlcore::SharedPtr<Node> expr2;
    Operator oper;
  };

  class BitOperNode : public Node {
  public:
    BitOperNode(Node *e1, Operator o, Node *e2) :
      expr1(e1), expr2(e2), oper(o) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr1;
    kxmlcore::SharedPtr<Node> expr2;
    Operator oper;
  };

  /**
   * expr1 && expr2, expr1 || expr2
   */
  class BinaryLogicalNode : public Node {
  public:
    BinaryLogicalNode(Node *e1, Operator o, Node *e2) :
      expr1(e1), expr2(e2), oper(o) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr1;
    kxmlcore::SharedPtr<Node> expr2;
    Operator oper;
  };

  /**
   * The ternary operator, "logical ? expr1 : expr2"
   */
  class ConditionalNode : public Node {
  public:
    ConditionalNode(Node *l, Node *e1, Node *e2) :
      logical(l), expr1(e1), expr2(e2) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> logical;
    kxmlcore::SharedPtr<Node> expr1;
    kxmlcore::SharedPtr<Node> expr2;
  };

  class AssignNode : public Node {
  public:
    AssignNode(Node *l, Operator o, Node *e) : left(l), oper(o), expr(e) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> left;
    Operator oper;
    kxmlcore::SharedPtr<Node> expr;
  };

  class CommaNode : public Node {
  public:
    CommaNode(Node *e1, Node *e2) : expr1(e1), expr2(e2) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr1;
    kxmlcore::SharedPtr<Node> expr2;
  };

  class StatListNode : public StatementNode {
  public:
    // list pointer is tail of a circular list, cracked in the CaseClauseNode ctor
    StatListNode(StatementNode *s);
    StatListNode(StatListNode *l, StatementNode *s);
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    friend class CaseClauseNode;
    kxmlcore::SharedPtr<StatementNode> statement;
    kxmlcore::SharedPtr<StatListNode> list;
  };

  class AssignExprNode : public Node {
  public:
    AssignExprNode(Node *e) : expr(e) {}
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
  };

  class VarDeclNode : public Node {
  public:
    VarDeclNode(const Identifier &id, AssignExprNode *in);
    Value evaluate(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    Identifier ident;
    kxmlcore::SharedPtr<AssignExprNode> init;
  };

  class VarDeclListNode : public Node {
  public:
    // list pointer is tail of a circular list, cracked in the ForNode/VarStatementNode ctor
    VarDeclListNode(VarDeclNode *v) : list(this), var(v) {}
    VarDeclListNode(VarDeclListNode *l, VarDeclNode *v)
      : list(l->list), var(v) { l->list = this; }
    Value evaluate(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    friend class ForNode;
    friend class VarStatementNode;
    kxmlcore::SharedPtr<VarDeclListNode> list;
    kxmlcore::SharedPtr<VarDeclNode> var;
  };

  class VarStatementNode : public StatementNode {
  public:
    VarStatementNode(VarDeclListNode *l)
      : list(l->list) { l->list = 0; }
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<VarDeclListNode> list;
  };

  class BlockNode : public StatementNode {
  public:
    BlockNode(SourceElementsNode *s);
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  protected:
    kxmlcore::SharedPtr<SourceElementsNode> source;
  };

  class EmptyStatementNode : public StatementNode {
  public:
    EmptyStatementNode() { } // debug
    virtual Completion execute(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  };

  class ExprStatementNode : public StatementNode {
  public:
    ExprStatementNode(Node *e) : expr(e) { }
    virtual Completion execute(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
  };

  class IfNode : public StatementNode {
  public:
    IfNode(Node *e, StatementNode *s1, StatementNode *s2)
      : expr(e), statement1(s1), statement2(s2) {}
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
    kxmlcore::SharedPtr<StatementNode> statement1;
    kxmlcore::SharedPtr<StatementNode> statement2;
  };

  class DoWhileNode : public StatementNode {
  public:
    DoWhileNode(StatementNode *s, Node *e) : statement(s), expr(e) {}
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<StatementNode> statement;
    kxmlcore::SharedPtr<Node> expr;
  };

  class WhileNode : public StatementNode {
  public:
    WhileNode(Node *e, StatementNode *s) : expr(e), statement(s) {}
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
    kxmlcore::SharedPtr<StatementNode> statement;
  };

  class ForNode : public StatementNode {
  public:
    ForNode(Node *e1, Node *e2, Node *e3, StatementNode *s) :
      expr1(e1), expr2(e2), expr3(e3), statement(s) {}
    ForNode(VarDeclListNode *e1, Node *e2, Node *e3, StatementNode *s) :
      expr1(e1->list), expr2(e2), expr3(e3), statement(s) { e1->list = 0; }
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr1;
    kxmlcore::SharedPtr<Node> expr2;
    kxmlcore::SharedPtr<Node> expr3;
    kxmlcore::SharedPtr<StatementNode> statement;
  };

  class ForInNode : public StatementNode {
  public:
    ForInNode(Node *l, Node *e, StatementNode *s);
    ForInNode(const Identifier &i, AssignExprNode *in, Node *e, StatementNode *s);
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    Identifier ident;
    kxmlcore::SharedPtr<AssignExprNode> init;
    kxmlcore::SharedPtr<Node> lexpr;
    kxmlcore::SharedPtr<Node> expr;
    kxmlcore::SharedPtr<VarDeclNode> varDecl;
    kxmlcore::SharedPtr<StatementNode> statement;
  };

  class ContinueNode : public StatementNode {
  public:
    ContinueNode() { }
    ContinueNode(const Identifier &i) : ident(i) { }
    virtual Completion execute(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    Identifier ident;
  };

  class BreakNode : public StatementNode {
  public:
    BreakNode() { }
    BreakNode(const Identifier &i) : ident(i) { }
    virtual Completion execute(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    Identifier ident;
  };

  class ReturnNode : public StatementNode {
  public:
    ReturnNode(Node *v) : value(v) {}
    virtual Completion execute(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> value;
  };

  class WithNode : public StatementNode {
  public:
    WithNode(Node *e, StatementNode *s) : expr(e), statement(s) {}
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
    kxmlcore::SharedPtr<StatementNode> statement;
  };

  class CaseClauseNode : public Node {
  public:
    CaseClauseNode(Node *e) : expr(e), list(0) { }
    CaseClauseNode(Node *e, StatListNode *l)
      : expr(e), list(l->list) { l->list = 0; }
    Value evaluate(ExecState *exec);
    Completion evalStatements(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
    kxmlcore::SharedPtr<StatListNode> list;
  };

  class ClauseListNode : public Node {
  public:
    // list pointer is tail of a circular list, cracked in the CaseBlockNode ctor
    ClauseListNode(CaseClauseNode *c) : cl(c), nx(this) { }
    ClauseListNode(ClauseListNode *n, CaseClauseNode *c)
      : cl(c), nx(n->nx) { n->nx = this; }
    Value evaluate(ExecState *exec);
    CaseClauseNode *clause() const { return cl.get(); }
    ClauseListNode *next() const { return nx.get(); }
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    friend class CaseBlockNode;
    kxmlcore::SharedPtr<CaseClauseNode> cl;
    kxmlcore::SharedPtr<ClauseListNode> nx;
  };

  class CaseBlockNode : public Node {
  public:
    CaseBlockNode(ClauseListNode *l1, CaseClauseNode *d, ClauseListNode *l2);
    Value evaluate(ExecState *exec);
    Completion evalBlock(ExecState *exec, const Value& input);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<ClauseListNode> list1;
    kxmlcore::SharedPtr<CaseClauseNode> def;
    kxmlcore::SharedPtr<ClauseListNode> list2;
  };

  class SwitchNode : public StatementNode {
  public:
    SwitchNode(Node *e, CaseBlockNode *b) : expr(e), block(b) { }
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
    kxmlcore::SharedPtr<CaseBlockNode> block;
  };

  class LabelNode : public StatementNode {
  public:
    LabelNode(const Identifier &l, StatementNode *s) : label(l), statement(s) { }
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    Identifier label;
    kxmlcore::SharedPtr<StatementNode> statement;
  };

  class ThrowNode : public StatementNode {
  public:
    ThrowNode(Node *e) : expr(e) {}
    virtual Completion execute(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<Node> expr;
  };

  class CatchNode : public StatementNode {
  public:
    CatchNode(const Identifier &i, StatementNode *b) : ident(i), block(b) {}
    virtual Completion execute(ExecState *exec);
    Completion execute(ExecState *exec, const Value &arg);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    Identifier ident;
    kxmlcore::SharedPtr<StatementNode> block;
  };

  class FinallyNode : public StatementNode {
  public:
    FinallyNode(StatementNode *b) : block(b) {}
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<StatementNode> block;
  };

  class TryNode : public StatementNode {
  public:
    TryNode(StatementNode *b, CatchNode *c)
      : block(b), _catch(c), _final(0) {}
    TryNode(StatementNode *b, FinallyNode *f)
      : block(b), _catch(0), _final(f) {}
    TryNode(StatementNode *b, CatchNode *c, FinallyNode *f)
      : block(b), _catch(c), _final(f) {}
    virtual Completion execute(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<StatementNode> block;
    kxmlcore::SharedPtr<CatchNode> _catch;
    kxmlcore::SharedPtr<FinallyNode> _final;
  };

  class ParameterNode : public Node {
  public:
    // list pointer is tail of a circular list, cracked in the FuncDeclNode/FuncExprNode ctor
    ParameterNode(const Identifier &i) : id(i), next(this) { }
    ParameterNode(ParameterNode *list, const Identifier &i)
      : id(i), next(list->next) { list->next = this; }
    Value evaluate(ExecState *exec);
    Identifier ident() { return id; }
    ParameterNode *nextParam() { return next.get(); }
    virtual void streamTo(SourceStream &s) const;
  private:
    friend class FuncDeclNode;
    friend class FuncExprNode;
    Identifier id;
    kxmlcore::SharedPtr<ParameterNode> next;
  };

  // inherited by ProgramNode
  class FunctionBodyNode : public BlockNode {
  public:
    FunctionBodyNode(SourceElementsNode *s);
    void processFuncDecl(ExecState *exec);
  };

  class FuncDeclNode : public StatementNode {
  public:
    FuncDeclNode(const Identifier &i, FunctionBodyNode *b)
      : ident(i), param(0), body(b) { }
    FuncDeclNode(const Identifier &i, ParameterNode *p, FunctionBodyNode *b)
      : ident(i), param(p->next), body(b) { p->next = 0; }
    Completion execute(ExecState */*exec*/)
      { /* empty */ return Completion(); }
    void processFuncDecl(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    Identifier ident;
    kxmlcore::SharedPtr<ParameterNode> param;
    kxmlcore::SharedPtr<FunctionBodyNode> body;
  };

  class FuncExprNode : public Node {
  public:
    FuncExprNode(FunctionBodyNode *b) : param(0), body(b) { }
    FuncExprNode(ParameterNode *p, FunctionBodyNode *b)
      : param(p->next), body(b) { p->next = 0; }
    Value evaluate(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    kxmlcore::SharedPtr<ParameterNode> param;
    kxmlcore::SharedPtr<FunctionBodyNode> body;
  };

  // A linked list of source element nodes
  class SourceElementsNode : public StatementNode {
  public:
    // list pointer is tail of a circular list, cracked in the BlockNode (or subclass) ctor
    SourceElementsNode(StatementNode *s1);
    SourceElementsNode(SourceElementsNode *s1, StatementNode *s2);

    Completion execute(ExecState *exec);
    void processFuncDecl(ExecState *exec);
    virtual void processVarDecls(ExecState *exec);
    virtual void streamTo(SourceStream &s) const;
  private:
    friend class BlockNode;
    kxmlcore::SharedPtr<StatementNode> element; // 'this' element
    kxmlcore::SharedPtr<SourceElementsNode> elements; // pointer to next
  };

  class ProgramNode : public FunctionBodyNode {
  public:
    ProgramNode(SourceElementsNode *s);
  };

}; // namespace

#endif
