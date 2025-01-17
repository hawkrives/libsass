#ifndef SASS_AST_H
#define SASS_AST_H

#include <set>
#include <deque>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <typeinfo>
#include <algorithm>
#include <unordered_map>

#ifdef __clang__

/*
 * There are some overloads used here that trigger the clang overload
 * hiding warning. Specifically:
 *
 * Type type() which hides string type() from Expression
 *
 * and
 *
 * Block* block() which hides virtual Block* block() from Statement
 *
 */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverloaded-virtual"

#endif

#include "util.hpp"
#include "units.hpp"
#include "context.hpp"
#include "position.hpp"
#include "constants.hpp"
#include "operation.hpp"
#include "position.hpp"
#include "inspect.hpp"
#include "source_map.hpp"
#include "environment.hpp"
#include "error_handling.hpp"
#include "ast_def_macros.hpp"
#include "ast_fwd_decl.hpp"
#include "to_string.hpp"
#include "source_map.hpp"

#include "sass.h"
#include "sass_values.h"
#include "sass_context.h"
#include "sass_functions.h"

namespace Sass {
  using namespace std;

  // from boost (functional/hash):
  // http://www.boost.org/doc/libs/1_35_0/doc/html/hash/combine.html
  // Boost Software License - Version 1.0
  // http://www.boost.org/users/license.html
  template <typename T>
  void hash_combine (std::size_t& seed, const T& val)
  {
    seed ^= std::hash<T>()(val) + 0x9e3779b9
             + (seed<<6) + (seed>>2);
  }

  //////////////////////////////////////////////////////////
  // Abstract base class for all abstract syntax tree nodes.
  //////////////////////////////////////////////////////////
  class AST_Node {
    ADD_PROPERTY(ParserState, pstate)
  public:
    AST_Node(ParserState pstate)
    : pstate_(pstate)
    { }
    virtual ~AST_Node() = 0;
    // virtual Block* block() { return 0; }
  public:
    void update_pstate(const ParserState& pstate);
  public:
    Offset off() { return pstate(); }
    Position pos() { return pstate(); }
    ATTACH_OPERATIONS()
  };
  inline AST_Node::~AST_Node() { }


  //////////////////////////////////////////////////////////////////////
  // Abstract base class for expressions. This side of the AST hierarchy
  // represents elements in value contexts, which exist primarily to be
  // evaluated and returned.
  //////////////////////////////////////////////////////////////////////
  class Expression : public AST_Node {
  public:
    enum Concrete_Type {
      NONE,
      BOOLEAN,
      NUMBER,
      COLOR,
      STRING,
      LIST,
      MAP,
      SELECTOR,
      NULL_VAL,
      C_WARNING,
      C_ERROR,
      NUM_TYPES
    };
  private:
    // expressions in some contexts shouldn't be evaluated
    ADD_PROPERTY(bool, is_delayed)
    ADD_PROPERTY(bool, is_expanded)
    ADD_PROPERTY(bool, is_interpolant)
    ADD_PROPERTY(Concrete_Type, concrete_type)
  public:
    Expression(ParserState pstate,
               bool d = false, bool e = false, bool i = false, Concrete_Type ct = NONE)
    : AST_Node(pstate),
      is_delayed_(d),
      is_expanded_(d),
      is_interpolant_(i),
      concrete_type_(ct)
    { }
    virtual operator bool() { return true; }
    virtual ~Expression() { }
    virtual string type() { return ""; /* TODO: raise an error? */ }
    virtual bool is_invisible() const { return false; }
    static string type_name() { return ""; }
    virtual bool is_false() { return false; }
    virtual bool operator== (const Expression& rhs) const { return false; }
    virtual void set_delayed(bool delayed) { is_delayed(delayed); }
    virtual size_t hash() { return 0; }
  };

  //////////////////////////////////////////////////////////////////////
  // base class for values that support operations
  //////////////////////////////////////////////////////////////////////
  class Value : public Expression {
  public:
    Value(ParserState pstate,
          bool d = false, bool e = false, bool i = false, Concrete_Type ct = NONE)
    : Expression(pstate, d, e, i, ct)
    { }
    virtual bool operator== (const Expression& rhs) const = 0;
    virtual bool operator== (const Expression* rhs) const = 0;
    virtual string to_string(bool compressed = false, int precision = 5) const = 0;
  };
}


/////////////////////////////////////////////////////////////////////////////
// Hash method specializations for unordered_map to work with Sass::Expression
/////////////////////////////////////////////////////////////////////////////

namespace std {
  template<>
  struct hash<Sass::Expression*>
  {
    size_t operator()(Sass::Expression* s) const
    {
      return s->hash();
    }
  };
  template<>
  struct equal_to<Sass::Expression*>
  {
    bool operator()( Sass::Expression* lhs,  Sass::Expression* rhs) const
    {
      return lhs->hash() == rhs->hash();
    }
  };
}

namespace Sass {
  using namespace std;

  /////////////////////////////////////////////////////////////////////////////
  // Mixin class for AST nodes that should behave like vectors. Uses the
  // "Template Method" design pattern to allow subclasses to adjust their flags
  // when certain objects are pushed.
  /////////////////////////////////////////////////////////////////////////////
  template <typename T>
  class Vectorized {
    vector<T> elements_;
  protected:
    size_t hash_;
    void reset_hash() { hash_ = 0; }
    virtual void adjust_after_pushing(T element) { }
  public:
    Vectorized(size_t s = 0) : elements_(vector<T>())
    { elements_.reserve(s); }
    virtual ~Vectorized() = 0;
    size_t length() const   { return elements_.size(); }
    bool empty() const      { return elements_.empty(); }
    T last()                { return elements_.back(); }
    T first()               { return elements_.front(); }
    T& operator[](size_t i) { return elements_[i]; }
    const T& operator[](size_t i) const { return elements_[i]; }
    Vectorized& operator<<(T element)
    {
      if (!element) return *this;
      reset_hash();
      elements_.push_back(element);
      adjust_after_pushing(element);
      return *this;
    }
    Vectorized& operator+=(Vectorized* v)
    {
      for (size_t i = 0, L = v->length(); i < L; ++i) *this << (*v)[i];
      return *this;
    }
    Vectorized& unshift(T element)
    {
      elements_.insert(elements_.begin(), element);
      return *this;
    }
    vector<T>& elements() { return elements_; }
    const vector<T>& elements() const { return elements_; }
    vector<T>& elements(vector<T>& e) { elements_ = e; return elements_; }

    typename vector<T>::iterator end() { return elements_.end(); }
    typename vector<T>::iterator begin() { return elements_.begin(); }
    typename vector<T>::const_iterator end() const { return elements_.end(); }
    typename vector<T>::const_iterator begin() const { return elements_.begin(); }

  };
  template <typename T>
  inline Vectorized<T>::~Vectorized() { }

  /////////////////////////////////////////////////////////////////////////////
  // Mixin class for AST nodes that should behave like a hash table. Uses an
  // extra <vector> internally to maintain insertion order for interation.
  /////////////////////////////////////////////////////////////////////////////
  class Hashed {
  private:
    unordered_map<Expression*, Expression*> elements_;
    vector<Expression*> list_;
  protected:
    size_t hash_;
    Expression* duplicate_key_;
    void reset_hash() { hash_ = 0; }
    void reset_duplicate_key() { duplicate_key_ = 0; }
    virtual void adjust_after_pushing(std::pair<Expression*, Expression*> p) { }
  public:
    Hashed(size_t s = 0) : elements_(unordered_map<Expression*, Expression*>(s)), list_(vector<Expression*>())
    { elements_.reserve(s); list_.reserve(s); reset_duplicate_key(); }
    virtual ~Hashed();
    size_t length() const                  { return list_.size(); }
    bool empty() const                     { return list_.empty(); }
    bool has(Expression* k) const          { return elements_.count(k) == 1; }
    Expression* at(Expression* k) const;
    bool has_duplicate_key() const         { return duplicate_key_ != 0; }
    Expression* get_duplicate_key() const  { return duplicate_key_; }
    const unordered_map<Expression*, Expression*> elements() { return elements_; }
    Hashed& operator<<(pair<Expression*, Expression*> p)
    {
      reset_hash();

      if (!has(p.first)) list_.push_back(p.first);
      else if (!duplicate_key_) duplicate_key_ = p.first;

      elements_[p.first] = p.second;

      adjust_after_pushing(p);
      return *this;
    }
    Hashed& operator+=(Hashed* h)
    {
      if (length() == 0) {
        this->elements_ = h->elements_;
        this->list_ = h->list_;
        return *this;
      }

      for (auto key : h->keys()) {
        *this << make_pair(key, h->at(key));
      }

      reset_duplicate_key();
      return *this;
    }
    const unordered_map<Expression*, Expression*>& pairs() const { return elements_; }
    const vector<Expression*>& keys() const { return list_; }

    unordered_map<Expression*, Expression*>::iterator end() { return elements_.end(); }
    unordered_map<Expression*, Expression*>::iterator begin() { return elements_.begin(); }
    unordered_map<Expression*, Expression*>::const_iterator end() const { return elements_.end(); }
    unordered_map<Expression*, Expression*>::const_iterator begin() const { return elements_.begin(); }

  };
  inline Hashed::~Hashed() { }


  /////////////////////////////////////////////////////////////////////////
  // Abstract base class for statements. This side of the AST hierarchy
  // represents elements in expansion contexts, which exist primarily to be
  // rewritten and macro-expanded.
  /////////////////////////////////////////////////////////////////////////
  class Statement : public AST_Node {
  public:
    enum Statement_Type {
      NONE,
      RULESET,
      MEDIA,
      DIRECTIVE,
      SUPPORTS,
      ATROOT,
      BUBBLE,
      KEYFRAMERULE
    };
  private:
    ADD_PROPERTY(Block*, block)
    ADD_PROPERTY(Statement_Type, statement_type)
    ADD_PROPERTY(size_t, tabs)
    ADD_PROPERTY(bool, group_end)
  public:
    Statement(ParserState pstate, Statement_Type st = NONE, size_t t = 0)
    : AST_Node(pstate), statement_type_(st), tabs_(t), group_end_(false)
     { }
    virtual ~Statement() = 0;
    // needed for rearranging nested rulesets during CSS emission
    virtual bool   is_hoistable() { return false; }
    virtual bool   is_invisible() const { return false; }
    virtual bool   bubbles() { return false; }
    virtual Block* block()  { return 0; }
  };
  inline Statement::~Statement() { }

  ////////////////////////
  // Blocks of statements.
  ////////////////////////
  class Block : public Statement, public Vectorized<Statement*> {
    ADD_PROPERTY(bool, is_root)
    ADD_PROPERTY(bool, is_at_root);
    // needed for properly formatted CSS emission
    ADD_PROPERTY(bool, has_hoistable)
    ADD_PROPERTY(bool, has_non_hoistable)
  protected:
    void adjust_after_pushing(Statement* s)
    {
      if (s->is_hoistable()) has_hoistable_     = true;
      else                   has_non_hoistable_ = true;
    }
  public:
    Block(ParserState pstate, size_t s = 0, bool r = false)
    : Statement(pstate),
      Vectorized<Statement*>(s),
      is_root_(r), is_at_root_(false), has_hoistable_(false), has_non_hoistable_(false)
    { }
    Block* block() { return this; }
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////////////////////
  // Abstract base class for statements that contain blocks of statements.
  ////////////////////////////////////////////////////////////////////////
  class Has_Block : public Statement {
    ADD_PROPERTY(Block*, block)
  public:
    Has_Block(ParserState pstate, Block* b)
    : Statement(pstate), block_(b)
    { }
    virtual ~Has_Block() = 0;
  };
  inline Has_Block::~Has_Block() { }

  /////////////////////////////////////////////////////////////////////////////
  // Rulesets (i.e., sets of styles headed by a selector and containing a block
  // of style declarations.
  /////////////////////////////////////////////////////////////////////////////
  class Ruleset : public Has_Block {
    ADD_PROPERTY(Selector*, selector)
    ADD_PROPERTY(bool, at_root);
  public:
    Ruleset(ParserState pstate, Selector* s = 0, Block* b = 0)
    : Has_Block(pstate, b), selector_(s), at_root_(false)
    { statement_type(RULESET); }
    bool is_invisible() const;
    // nested rulesets need to be hoisted out of their enclosing blocks
    bool is_hoistable() { return true; }
    ATTACH_OPERATIONS()
  };

  /////////////////////////////////////////////////////////
  // Nested declaration sets (i.e., namespaced properties).
  /////////////////////////////////////////////////////////
  class Propset : public Has_Block {
    ADD_PROPERTY(String*, property_fragment)
  public:
    Propset(ParserState pstate, String* pf, Block* b = 0)
    : Has_Block(pstate, b), property_fragment_(pf)
    { }
    ATTACH_OPERATIONS()
  };

  /////////////////
  // Bubble.
  /////////////////
  class Bubble : public Statement {
    ADD_PROPERTY(Statement*, node)
    ADD_PROPERTY(bool, group_end)
  public:
    Bubble(ParserState pstate, Statement* n, Statement* g = 0, size_t t = 0)
    : Statement(pstate, Statement::BUBBLE, t), node_(n), group_end_(g == 0)
    { }
    bool bubbles() { return true; }
    ATTACH_OPERATIONS()
  };

  /////////////////
  // Media queries.
  /////////////////
  class Media_Block : public Has_Block {
    ADD_PROPERTY(List*, media_queries)
  public:
    Media_Block(ParserState pstate, List* mqs, Block* b)
    : Has_Block(pstate, b), media_queries_(mqs)
    { statement_type(MEDIA); }
    Media_Block(ParserState pstate, List* mqs, Block* b, Selector* s)
    : Has_Block(pstate, b), media_queries_(mqs)
    { statement_type(MEDIA); }
    bool bubbles() { return true; }
    bool is_hoistable() { return true; }
    bool is_invisible() const {
      bool is_invisible = true;
      for (size_t i = 0, L = block()->length(); i < L && is_invisible; i++)
        is_invisible &= (*block())[i]->is_invisible();
      return is_invisible;
    }
    ATTACH_OPERATIONS()
  };

  //////////////////
  // Query features.
  //////////////////
  class Supports_Block : public Has_Block {
    ADD_PROPERTY(Supports_Query*, queries)
  public:
    Supports_Block(ParserState pstate, Supports_Query* queries = 0, Block* block = 0)
    : Has_Block(pstate, block), queries_(queries)
    { statement_type(SUPPORTS); }
    bool is_hoistable() { return true; }
    bool bubbles() { return true; }
    ATTACH_OPERATIONS()
  };

  ///////////////////////////////////////////////////////////////////////
  // At-rules -- arbitrary directives beginning with "@" that may have an
  // optional statement block.
  ///////////////////////////////////////////////////////////////////////
  class At_Rule : public Has_Block {
    ADD_PROPERTY(string, keyword)
    ADD_PROPERTY(Selector*, selector)
    ADD_PROPERTY(Expression*, value)
  public:
    At_Rule(ParserState pstate, string kwd, Selector* sel = 0, Block* b = 0, Expression* val = 0)
    : Has_Block(pstate, b), keyword_(kwd), selector_(sel), value_(val) // set value manually if needed
    { statement_type(DIRECTIVE); }
    bool bubbles() { return is_keyframes() || is_media(); }
    bool is_media() {
      return keyword_.compare("@-webkit-media") == 0 ||
             keyword_.compare("@-moz-media") == 0 ||
             keyword_.compare("@-o-media") == 0 ||
             keyword_.compare("@media") == 0;
    }
    bool is_keyframes() {
      return keyword_.compare("@-webkit-keyframes") == 0 ||
             keyword_.compare("@-moz-keyframes") == 0 ||
             keyword_.compare("@-o-keyframes") == 0 ||
             keyword_.compare("@keyframes") == 0;
    }
    ATTACH_OPERATIONS()
  };

  ///////////////////////////////////////////////////////////////////////
  // Keyframe-rules -- the child blocks of "@keyframes" nodes.
  ///////////////////////////////////////////////////////////////////////
  class Keyframe_Rule : public Has_Block {
    ADD_PROPERTY(Selector*, selector)
  public:
    Keyframe_Rule(ParserState pstate, Block* b)
    : Has_Block(pstate, b), selector_(0)
    { statement_type(KEYFRAMERULE); }
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////////////////////
  // Declarations -- style rules consisting of a property name and values.
  ////////////////////////////////////////////////////////////////////////
  class Declaration : public Statement {
    ADD_PROPERTY(String*, property)
    ADD_PROPERTY(Expression*, value)
    ADD_PROPERTY(bool, is_important)
    ADD_PROPERTY(bool, is_indented)
  public:
    Declaration(ParserState pstate,
                String* prop, Expression* val, bool i = false)
    : Statement(pstate), property_(prop), value_(val), is_important_(i), is_indented_(false)
    { }
    ATTACH_OPERATIONS()
  };

  /////////////////////////////////////
  // Assignments -- variable and value.
  /////////////////////////////////////
  class Assignment : public Statement {
    ADD_PROPERTY(string, variable)
    ADD_PROPERTY(Expression*, value)
    ADD_PROPERTY(bool, is_default)
    ADD_PROPERTY(bool, is_global)
  public:
    Assignment(ParserState pstate,
               string var, Expression* val,
               bool is_default = false,
               bool is_global = false)
    : Statement(pstate), variable_(var), value_(val), is_default_(is_default), is_global_(is_global)
    { }
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////////////////////////
  // Import directives. CSS and Sass import lists can be intermingled, so it's
  // necessary to store a list of each in an Import node.
  ////////////////////////////////////////////////////////////////////////////
  class Import : public Statement {
    vector<string>         files_;
    vector<Expression*>    urls_;
    ADD_PROPERTY(List*, media_queries);
  public:
    Import(ParserState pstate)
    : Statement(pstate),
      files_(vector<string>()),
      urls_(vector<Expression*>()),
      media_queries_(0)
    { }
    vector<string>&      files()    { return files_; }
    vector<Expression*>& urls()     { return urls_; }
    ATTACH_OPERATIONS()
  };

  class Import_Stub : public Statement {
    ADD_PROPERTY(string, file_name)
  public:
    Import_Stub(ParserState pstate, string f)
    : Statement(pstate), file_name_(f)
    { }
    ATTACH_OPERATIONS()
  };

  //////////////////////////////
  // The Sass `@warn` directive.
  //////////////////////////////
  class Warning : public Statement {
    ADD_PROPERTY(Expression*, message)
  public:
    Warning(ParserState pstate, Expression* msg)
    : Statement(pstate), message_(msg)
    { }
    ATTACH_OPERATIONS()
  };

  ///////////////////////////////
  // The Sass `@error` directive.
  ///////////////////////////////
  class Error : public Statement {
    ADD_PROPERTY(Expression*, message)
  public:
    Error(ParserState pstate, Expression* msg)
    : Statement(pstate), message_(msg)
    { }
    ATTACH_OPERATIONS()
  };

  ///////////////////////////////
  // The Sass `@debug` directive.
  ///////////////////////////////
  class Debug : public Statement {
    ADD_PROPERTY(Expression*, value)
  public:
    Debug(ParserState pstate, Expression* val)
    : Statement(pstate), value_(val)
    { }
    ATTACH_OPERATIONS()
  };

  ///////////////////////////////////////////
  // CSS comments. These may be interpolated.
  ///////////////////////////////////////////
  class Comment : public Statement {
    ADD_PROPERTY(String*, text)
    ADD_PROPERTY(bool, is_important)
  public:
    Comment(ParserState pstate, String* txt, bool is_important)
    : Statement(pstate), text_(txt), is_important_(is_important)
    { }
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////
  // The Sass `@if` control directive.
  ////////////////////////////////////
  class If : public Has_Block {
    ADD_PROPERTY(Expression*, predicate)
    ADD_PROPERTY(Block*, alternative)
  public:
    If(ParserState pstate, Expression* pred, Block* con, Block* alt = 0)
    : Has_Block(pstate, con), predicate_(pred), alternative_(alt)
    { }
    ATTACH_OPERATIONS()
  };

  /////////////////////////////////////
  // The Sass `@for` control directive.
  /////////////////////////////////////
  class For : public Has_Block {
    ADD_PROPERTY(string, variable)
    ADD_PROPERTY(Expression*, lower_bound)
    ADD_PROPERTY(Expression*, upper_bound)
    ADD_PROPERTY(bool, is_inclusive)
  public:
    For(ParserState pstate,
        string var, Expression* lo, Expression* hi, Block* b, bool inc)
    : Has_Block(pstate, b),
      variable_(var), lower_bound_(lo), upper_bound_(hi), is_inclusive_(inc)
    { }
    ATTACH_OPERATIONS()
  };

  //////////////////////////////////////
  // The Sass `@each` control directive.
  //////////////////////////////////////
  class Each : public Has_Block {
    ADD_PROPERTY(vector<string>, variables)
    ADD_PROPERTY(Expression*, list)
  public:
    Each(ParserState pstate, vector<string> vars, Expression* lst, Block* b)
    : Has_Block(pstate, b), variables_(vars), list_(lst)
    { }
    ATTACH_OPERATIONS()
  };

  ///////////////////////////////////////
  // The Sass `@while` control directive.
  ///////////////////////////////////////
  class While : public Has_Block {
    ADD_PROPERTY(Expression*, predicate)
  public:
    While(ParserState pstate, Expression* pred, Block* b)
    : Has_Block(pstate, b), predicate_(pred)
    { }
    ATTACH_OPERATIONS()
  };

  /////////////////////////////////////////////////////////////
  // The @return directive for use inside SassScript functions.
  /////////////////////////////////////////////////////////////
  class Return : public Statement {
    ADD_PROPERTY(Expression*, value)
  public:
    Return(ParserState pstate, Expression* val)
    : Statement(pstate), value_(val)
    { }
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////
  // The Sass `@extend` directive.
  ////////////////////////////////
  class Extension : public Statement {
    ADD_PROPERTY(Selector*, selector)
  public:
    Extension(ParserState pstate, Selector* s)
    : Statement(pstate), selector_(s)
    { }
    ATTACH_OPERATIONS()
  };

  /////////////////////////////////////////////////////////////////////////////
  // Definitions for both mixins and functions. The two cases are distinguished
  // by a type tag.
  /////////////////////////////////////////////////////////////////////////////
  struct Backtrace;
  typedef Environment<AST_Node*> Env;
  typedef const char* Signature;
  typedef Expression* (*Native_Function)(Env&, Env&, Context&, Signature, ParserState, Backtrace*);
  typedef const char* Signature;
  class Definition : public Has_Block {
  public:
    enum Type { MIXIN, FUNCTION };
    ADD_PROPERTY(string, name)
    ADD_PROPERTY(Parameters*, parameters)
    ADD_PROPERTY(Env*, environment)
    ADD_PROPERTY(Type, type)
    ADD_PROPERTY(Native_Function, native_function)
    ADD_PROPERTY(Sass_Function_Entry, c_function)
    ADD_PROPERTY(void*, cookie)
    ADD_PROPERTY(bool, is_overload_stub)
    ADD_PROPERTY(Signature, signature)
  public:
    Definition(ParserState pstate,
               string n,
               Parameters* params,
               Block* b,
               Type t)
    : Has_Block(pstate, b),
      name_(n),
      parameters_(params),
      environment_(0),
      type_(t),
      native_function_(0),
      c_function_(0),
      cookie_(0),
      is_overload_stub_(false),
      signature_(0)
    { }
    Definition(ParserState pstate,
               Signature sig,
               string n,
               Parameters* params,
               Native_Function func_ptr,
               bool overload_stub = false)
    : Has_Block(pstate, 0),
      name_(n),
      parameters_(params),
      environment_(0),
      type_(FUNCTION),
      native_function_(func_ptr),
      c_function_(0),
      cookie_(0),
      is_overload_stub_(overload_stub),
      signature_(sig)
    { }
    Definition(ParserState pstate,
               Signature sig,
               string n,
               Parameters* params,
               Sass_Function_Entry c_func,
               bool whatever,
               bool whatever2)
    : Has_Block(pstate, 0),
      name_(n),
      parameters_(params),
      environment_(0),
      type_(FUNCTION),
      native_function_(0),
      c_function_(c_func),
      cookie_(sass_function_get_cookie(c_func)),
      is_overload_stub_(false),
      signature_(sig)
    { }
    ATTACH_OPERATIONS()
  };

  //////////////////////////////////////
  // Mixin calls (i.e., `@include ...`).
  //////////////////////////////////////
  class Mixin_Call : public Has_Block {
    ADD_PROPERTY(string, name)
    ADD_PROPERTY(Arguments*, arguments)
  public:
    Mixin_Call(ParserState pstate, string n, Arguments* args, Block* b = 0)
    : Has_Block(pstate, b), name_(n), arguments_(args)
    { }
    ATTACH_OPERATIONS()
  };

  ///////////////////////////////////////////////////
  // The @content directive for mixin content blocks.
  ///////////////////////////////////////////////////
  class Content : public Statement {
  public:
    Content(ParserState pstate) : Statement(pstate) { }
    ATTACH_OPERATIONS()
  };

  ///////////////////////////////////////////////////////////////////////
  // Lists of values, both comma- and space-separated (distinguished by a
  // type-tag.) Also used to represent variable-length argument lists.
  ///////////////////////////////////////////////////////////////////////
  class List : public Value, public Vectorized<Expression*> {
    void adjust_after_pushing(Expression* e) { is_expanded(false); }
  private:
    ADD_PROPERTY(enum Sass_Separator, separator)
    ADD_PROPERTY(bool, is_arglist)
  public:
    List(ParserState pstate,
         size_t size = 0, enum Sass_Separator sep = SASS_SPACE, bool argl = false)
    : Value(pstate),
      Vectorized<Expression*>(size),
      separator_(sep), is_arglist_(argl)
    { concrete_type(LIST); }
    string type() { return is_arglist_ ? "arglist" : "list"; }
    static string type_name() { return "list"; }
    const char* sep_string(bool compressed = false) const {
      return separator() == SASS_COMMA ?
        (compressed ? "," : ", ") : " ";
    }
    bool is_invisible() const { return empty(); }
    Expression* value_at_index(size_t i);

    virtual size_t size() const;

    virtual size_t hash()
    {
      if (hash_ == 0) {
        hash_ = std::hash<string>()(sep_string());
        for (size_t i = 0, L = length(); i < L; ++i)
          hash_combine(hash_, (elements()[i])->hash());
      }
      return hash_;
    }

    virtual void set_delayed(bool delayed)
    {
      for (size_t i = 0, L = length(); i < L; ++i)
        (elements()[i])->set_delayed(delayed);
      is_delayed(delayed);
    }

    virtual bool operator== (const Expression& rhs) const;
    virtual bool operator== (const Expression* rhs) const;
    virtual string to_string(bool compressed = false, int precision = 5) const;

    ATTACH_OPERATIONS()
  };

  ///////////////////////////////////////////////////////////////////////
  // Key value paris.
  ///////////////////////////////////////////////////////////////////////
  class Map : public Value, public Hashed {
    void adjust_after_pushing(std::pair<Expression*, Expression*> p) { is_expanded(false); }
  public:
    Map(ParserState pstate,
         size_t size = 0)
    : Value(pstate),
      Hashed(size)
    { concrete_type(MAP); }
    string type() { return "map"; }
    static string type_name() { return "map"; }
    bool is_invisible() const { return empty(); }

    virtual size_t hash()
    {
      if (hash_ == 0) {
        for (auto key : keys()) {
          hash_combine(hash_, key->hash());
          hash_combine(hash_, at(key)->hash());
        }
      }

      return hash_;
    }

    virtual bool operator== (const Expression& rhs) const;
    virtual bool operator== (const Expression* rhs) const;
    virtual string to_string(bool compressed = false, int precision = 5) const;

    ATTACH_OPERATIONS()
  };

  //////////////////////////////////////////////////////////////////////////
  // Binary expressions. Represents logical, relational, and arithmetic
  // operations. Templatized to avoid large switch statements and repetitive
  // subclassing.
  //////////////////////////////////////////////////////////////////////////
  class Binary_Expression : public Expression {
  private:
    ADD_PROPERTY(enum Sass_OP, type)
    ADD_PROPERTY(Expression*, left)
    ADD_PROPERTY(Expression*, right)
    size_t hash_;
  public:
    Binary_Expression(ParserState pstate,
                      enum Sass_OP t, Expression* lhs, Expression* rhs)
    : Expression(pstate), type_(t), left_(lhs), right_(rhs), hash_(0)
    { }
    const string type_name() {
      switch (type_) {
        case AND: return "and"; break;
        case OR: return "or"; break;
        case EQ: return "eq"; break;
        case NEQ: return "neq"; break;
        case GT: return "gt"; break;
        case GTE: return "gte"; break;
        case LT: return "lt"; break;
        case LTE: return "lte"; break;
        case ADD: return "add"; break;
        case SUB: return "sub"; break;
        case MUL: return "mul"; break;
        case DIV: return "div"; break;
        case MOD: return "mod"; break;
        case NUM_OPS: return "num_ops"; break;
        default: return "invalid"; break;
      }
    }
    virtual void set_delayed(bool delayed)
    {
      right()->set_delayed(delayed);
      left()->set_delayed(delayed);
      is_delayed(delayed);
    }
    virtual bool operator==(const Expression& rhs) const
    {
      try
      {
        const Binary_Expression* m = dynamic_cast<const Binary_Expression*>(&rhs);
        if (m == 0) return false;
        return type() == m->type() &&
               left() == m->left() &&
               right() == m->right();
      }
      catch (std::bad_cast&)
      {
        return false;
      }
      catch (...) { throw; }
    }
    virtual size_t hash()
    {
      if (hash_ == 0) {
        hash_ = std::hash<size_t>()(type_);
        hash_combine(hash_, left()->hash());
        hash_combine(hash_, right()->hash());
      }
      return hash_;
    }
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////////////////////////
  // Arithmetic negation (logical negation is just an ordinary function call).
  ////////////////////////////////////////////////////////////////////////////
  class Unary_Expression : public Expression {
  public:
    enum Type { PLUS, MINUS, NOT };
  private:
    ADD_PROPERTY(Type, type)
    ADD_PROPERTY(Expression*, operand)
    size_t hash_;
  public:
    Unary_Expression(ParserState pstate, Type t, Expression* o)
    : Expression(pstate), type_(t), operand_(o), hash_(0)
    { }
    const string type_name() {
      switch (type_) {
        case PLUS: return "plus"; break;
        case MINUS: return "minus"; break;
        case NOT: return "not"; break;
        default: return "invalid"; break;
      }
    }
    virtual bool operator==(const Expression& rhs) const
    {
      try
      {
        const Unary_Expression* m = dynamic_cast<const Unary_Expression*>(&rhs);
        if (m == 0) return false;
        return type() == m->type() &&
               operand() == m->operand();
      }
      catch (std::bad_cast&)
      {
        return false;
      }
      catch (...) { throw; }
    }
    virtual size_t hash()
    {
      if (hash_ == 0) {
        hash_ = std::hash<size_t>()(type_);
        hash_combine(hash_, operand()->hash());
      };
      return hash_;
    }
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////////
  // Individual argument objects for mixin and function calls.
  ////////////////////////////////////////////////////////////
  class Argument : public Expression {
    ADD_PROPERTY(Expression*, value)
    ADD_PROPERTY(string, name)
    ADD_PROPERTY(bool, is_rest_argument)
    ADD_PROPERTY(bool, is_keyword_argument)
    size_t hash_;
  public:
    Argument(ParserState pstate, Expression* val, string n = "", bool rest = false, bool keyword = false)
    : Expression(pstate), value_(val), name_(n), is_rest_argument_(rest), is_keyword_argument_(keyword), hash_(0)
    {
      if (!name_.empty() && is_rest_argument_) {
        error("variable-length argument may not be passed by name", pstate);
      }
    }

    virtual bool operator==(const Expression& rhs) const
    {
      try
      {
        const Argument* m = dynamic_cast<const Argument*>(&rhs);
        if (!(m && name() == m->name())) return false;
        return *value() == *m->value();
      }
      catch (std::bad_cast&)
      {
        return false;
      }
      catch (...) { throw; }
    }

    virtual size_t hash()
    {
      if (hash_ == 0) {
        hash_ = std::hash<string>()(name());
        hash_combine(hash_, value()->hash());
      }
      return hash_;
    }

    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////////////////////
  // Argument lists -- in their own class to facilitate context-sensitive
  // error checking (e.g., ensuring that all ordinal arguments precede all
  // named arguments).
  ////////////////////////////////////////////////////////////////////////
  class Arguments : public Expression, public Vectorized<Argument*> {
    ADD_PROPERTY(bool, has_named_arguments)
    ADD_PROPERTY(bool, has_rest_argument)
    ADD_PROPERTY(bool, has_keyword_argument)
  protected:
    void adjust_after_pushing(Argument* a);
  public:
    Arguments(ParserState pstate)
    : Expression(pstate),
      Vectorized<Argument*>(),
      has_named_arguments_(false),
      has_rest_argument_(false),
      has_keyword_argument_(false)
    { }
    ATTACH_OPERATIONS()
  };

  //////////////////
  // Function calls.
  //////////////////
  class Function_Call : public Expression {
    ADD_PROPERTY(string, name)
    ADD_PROPERTY(Arguments*, arguments)
    ADD_PROPERTY(void*, cookie)
    size_t hash_;
  public:
    Function_Call(ParserState pstate, string n, Arguments* args, void* cookie)
    : Expression(pstate), name_(n), arguments_(args), cookie_(cookie), hash_(0)
    { concrete_type(STRING); }
    Function_Call(ParserState pstate, string n, Arguments* args)
    : Expression(pstate), name_(n), arguments_(args), cookie_(0), hash_(0)
    { concrete_type(STRING); }

    virtual bool operator==(const Expression& rhs) const
    {
      try
      {
        const Function_Call* m = dynamic_cast<const Function_Call*>(&rhs);
        if (!(m && name() == m->name())) return false;
        if (!(m && arguments()->length() == m->arguments()->length())) return false;
        for (size_t i =0, L = arguments()->length(); i < L; ++i)
          if (!((*arguments())[i] == (*m->arguments())[i])) return false;
        return true;
      }
      catch (std::bad_cast&)
      {
        return false;
      }
      catch (...) { throw; }
    }

    virtual size_t hash()
    {
      if (hash_ == 0) {
        hash_ = std::hash<string>()(name());
        for (auto argument : arguments()->elements())
          hash_combine(hash_, argument->hash());
      }
      return hash_;
    }

    ATTACH_OPERATIONS()
  };

  /////////////////////////
  // Function call schemas.
  /////////////////////////
  class Function_Call_Schema : public Expression {
    ADD_PROPERTY(String*, name)
    ADD_PROPERTY(Arguments*, arguments)
  public:
    Function_Call_Schema(ParserState pstate, String* n, Arguments* args)
    : Expression(pstate), name_(n), arguments_(args)
    { concrete_type(STRING); }
    ATTACH_OPERATIONS()
  };

  ///////////////////////
  // Variable references.
  ///////////////////////
  class Variable : public Expression {
    ADD_PROPERTY(string, name)
  public:
    Variable(ParserState pstate, string n)
    : Expression(pstate), name_(n)
    { }

    virtual bool operator==(const Expression& rhs) const
    {
      try
      {
        const Variable* e = dynamic_cast<const Variable*>(&rhs);
        return e && name() == e->name();
      }
      catch (std::bad_cast&)
      {
        return false;
      }
      catch (...) { throw; }
    }

    virtual size_t hash()
    {
      return std::hash<string>()(name());
    }

    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////////////////////////
  // Textual (i.e., unevaluated) numeric data. Variants are distinguished with
  // a type tag.
  ////////////////////////////////////////////////////////////////////////////
  class Textual : public Expression {
  public:
    enum Type { NUMBER, PERCENTAGE, DIMENSION, HEX };
  private:
    ADD_PROPERTY(Type, type)
    ADD_PROPERTY(string, value)
    size_t hash_;
  public:
    Textual(ParserState pstate, Type t, string val)
    : Expression(pstate, true), type_(t), value_(val),
      hash_(0)
    { }

    virtual bool operator==(const Expression& rhs) const
    {
      try
      {
        const Textual* e = dynamic_cast<const Textual*>(&rhs);
        return e && value() == e->value() && type() == e->type();
      }
      catch (std::bad_cast&)
      {
        return false;
      }
      catch (...) { throw; }
    }

    virtual size_t hash()
    {
      if (hash_ == 0) {
        hash_ = std::hash<string>()(value_);
        hash_combine(hash_, std::hash<int>()(type_));
      }
      return hash_;
    }

    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////
  // Numbers, percentages, dimensions, and colors.
  ////////////////////////////////////////////////
  class Number : public Value {
    ADD_PROPERTY(double, value)
    ADD_PROPERTY(bool, zero)
    vector<string> numerator_units_;
    vector<string> denominator_units_;
    size_t hash_;
  public:
    Number(ParserState pstate, double val, string u = "", bool zero = true);
    bool            zero()              { return zero_; }
    vector<string>& numerator_units()   { return numerator_units_; }
    vector<string>& denominator_units() { return denominator_units_; }
    const vector<string>& numerator_units() const   { return numerator_units_; }
    const vector<string>& denominator_units() const { return denominator_units_; }
    string type() { return "number"; }
    static string type_name() { return "number"; }
    string unit() const;

    bool is_unitless();
    void convert(const string& unit = "");
    void normalize(const string& unit = "");
    // useful for making one number compatible with another
    string find_convertible_unit() const;

    virtual size_t hash()
    {
      if (hash_ == 0) {
        hash_ = std::hash<double>()(value_);
      }
      return hash_;
    }

    virtual bool operator< (const Number& rhs) const;
    virtual bool operator< (const Number* rhs) const;
    virtual bool operator== (const Expression& rhs) const;
    virtual bool operator== (const Expression* rhs) const;
    virtual string to_string(bool compressed = false, int precision = 5) const;

    ATTACH_OPERATIONS()
  };

  //////////
  // Colors.
  //////////
  class Color : public Value {
    ADD_PROPERTY(double, r)
    ADD_PROPERTY(double, g)
    ADD_PROPERTY(double, b)
    ADD_PROPERTY(double, a)
    ADD_PROPERTY(bool, sixtuplet)
    ADD_PROPERTY(string, disp)
    size_t hash_;
  public:
    Color(ParserState pstate, double r, double g, double b, double a = 1, bool sixtuplet = true, const string disp = "")
    : Value(pstate), r_(r), g_(g), b_(b), a_(a), sixtuplet_(sixtuplet), disp_(disp),
      hash_(0)
    { concrete_type(COLOR); }
    string type() { return "color"; }
    static string type_name() { return "color"; }

    virtual size_t hash()
    {
      if (hash_ == 0) {
        hash_ = std::hash<double>()(a_);
        hash_combine(hash_, std::hash<double>()(r_));
        hash_combine(hash_, std::hash<double>()(g_));
        hash_combine(hash_, std::hash<double>()(b_));
      }
      return hash_;
    }

    virtual bool operator== (const Expression& rhs) const;
    virtual bool operator== (const Expression* rhs) const;
    virtual string to_string(bool compressed = false, int precision = 5) const;

    ATTACH_OPERATIONS()
  };

  //////////////////////////////
  // Errors from Sass_Values.
  //////////////////////////////
  class Custom_Error : public Value {
    ADD_PROPERTY(string, message)
  public:
    Custom_Error(ParserState pstate, string msg)
    : Value(pstate), message_(msg)
    { concrete_type(C_ERROR); }
    virtual bool operator== (const Expression& rhs) const;
    virtual bool operator== (const Expression* rhs) const;
    virtual string to_string(bool compressed = false, int precision = 5) const;
    ATTACH_OPERATIONS()
  };

  //////////////////////////////
  // Warnings from Sass_Values.
  //////////////////////////////
  class Custom_Warning : public Value {
    ADD_PROPERTY(string, message)
  public:
    Custom_Warning(ParserState pstate, string msg)
    : Value(pstate), message_(msg)
    { concrete_type(C_WARNING); }
    virtual bool operator== (const Expression& rhs) const;
    virtual bool operator== (const Expression* rhs) const;
    virtual string to_string(bool compressed = false, int precision = 5) const;
    ATTACH_OPERATIONS()
  };

  ////////////
  // Booleans.
  ////////////
  class Boolean : public Value {
    ADD_PROPERTY(bool, value)
    size_t hash_;
  public:
    Boolean(ParserState pstate, bool val)
    : Value(pstate), value_(val),
      hash_(0)
    { concrete_type(BOOLEAN); }
    virtual operator bool() { return value_; }
    string type() { return "bool"; }
    static string type_name() { return "bool"; }
    virtual bool is_false() { return !value_; }

    virtual size_t hash()
    {
      if (hash_ == 0) {
        hash_ = std::hash<bool>()(value_);
      }
      return hash_;
    }

    virtual bool operator== (const Expression& rhs) const;
    virtual bool operator== (const Expression* rhs) const;
    virtual string to_string(bool compressed = false, int precision = 5) const;

    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////////////////////
  // Abstract base class for Sass string values. Includes interpolated and
  // "flat" strings.
  ////////////////////////////////////////////////////////////////////////
  class String : public Value {
    ADD_PROPERTY(bool, sass_fix_1291)
  public:
    String(ParserState pstate, bool delayed = false, bool sass_fix_1291 = false)
    : Value(pstate, delayed), sass_fix_1291_(sass_fix_1291)
    { concrete_type(STRING); }
    static string type_name() { return "string"; }
    virtual ~String() = 0;
    virtual bool operator==(const Expression& rhs) const = 0;
    virtual bool operator==(const Expression* rhs) const = 0;
    virtual string to_string(bool compressed = false, int precision = 5) const = 0;
    ATTACH_OPERATIONS()
  };
  inline String::~String() { };

  ///////////////////////////////////////////////////////////////////////
  // Interpolated strings. Meant to be reduced to flat strings during the
  // evaluation phase.
  ///////////////////////////////////////////////////////////////////////
  class String_Schema : public String, public Vectorized<Expression*> {
    ADD_PROPERTY(bool, has_interpolants)
    size_t hash_;
  public:
    String_Schema(ParserState pstate, size_t size = 0, bool has_interpolants = false)
    : String(pstate), Vectorized<Expression*>(size), has_interpolants_(has_interpolants), hash_(0)
    { }
    string type() { return "string"; }
    static string type_name() { return "string"; }

    virtual size_t hash()
    {
      if (hash_ == 0) {
        for (auto string : elements())
          hash_combine(hash_, string->hash());
      }
      return hash_;
    }

    virtual bool operator==(const Expression& rhs) const;
    virtual bool operator==(const Expression* rhs) const;
    virtual string to_string(bool compressed = false, int precision = 5) const;

    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////
  // Flat strings -- the lowest level of raw textual data.
  ////////////////////////////////////////////////////////
  class String_Constant : public String {
    ADD_PROPERTY(char, quote_mark)
    ADD_PROPERTY(bool, can_compress_whitespace)
    ADD_PROPERTY(string, value)
  protected:
    size_t hash_;
  public:
    String_Constant(ParserState pstate, string val)
    : String(pstate), quote_mark_(0), can_compress_whitespace_(false), value_(read_css_string(val)), hash_(0)
    { }
    String_Constant(ParserState pstate, const char* beg)
    : String(pstate), quote_mark_(0), can_compress_whitespace_(false), value_(read_css_string(string(beg))), hash_(0)
    { }
    String_Constant(ParserState pstate, const char* beg, const char* end)
    : String(pstate), quote_mark_(0), can_compress_whitespace_(false), value_(read_css_string(string(beg, end-beg))), hash_(0)
    { }
    String_Constant(ParserState pstate, const Token& tok)
    : String(pstate), quote_mark_(0), can_compress_whitespace_(false), value_(read_css_string(string(tok.begin, tok.end))), hash_(0)
    { }
    string type() { return "string"; }
    static string type_name() { return "string"; }

    virtual size_t hash()
    {
      if (hash_ == 0) {
        hash_ = std::hash<string>()(value_);
      }
      return hash_;
    }

    virtual bool operator==(const Expression& rhs) const;
    virtual bool operator==(const Expression* rhs) const;
    virtual string to_string(bool compressed = false, int precision = 5) const;

    // static char auto_quote() { return '*'; }
    static char double_quote() { return '"'; }
    static char single_quote() { return '\''; }

    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////
  // Possibly quoted string (unquote on instantiation)
  ////////////////////////////////////////////////////////
  class String_Quoted : public String_Constant {
  public:
    String_Quoted(ParserState pstate, string val)
    : String_Constant(pstate, val)
    {
      value_ = unquote(value_, &quote_mark_);
    }
    virtual bool operator==(const Expression& rhs) const;
    virtual bool operator==(const Expression* rhs) const;
    virtual string to_string(bool compressed = false, int precision = 5) const;
    ATTACH_OPERATIONS()
  };

  /////////////////
  // Media queries.
  /////////////////
  class Media_Query : public Expression,
                      public Vectorized<Media_Query_Expression*> {
    ADD_PROPERTY(String*, media_type)
    ADD_PROPERTY(bool, is_negated)
    ADD_PROPERTY(bool, is_restricted)
  public:
    Media_Query(ParserState pstate,
                String* t = 0, size_t s = 0, bool n = false, bool r = false)
    : Expression(pstate), Vectorized<Media_Query_Expression*>(s),
      media_type_(t), is_negated_(n), is_restricted_(r)
    { }
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////
  // Media expressions (for use inside media queries).
  ////////////////////////////////////////////////////
  class Media_Query_Expression : public Expression {
    ADD_PROPERTY(Expression*, feature)
    ADD_PROPERTY(Expression*, value)
    ADD_PROPERTY(bool, is_interpolated)
  public:
    Media_Query_Expression(ParserState pstate,
                           Expression* f, Expression* v, bool i = false)
    : Expression(pstate), feature_(f), value_(v), is_interpolated_(i)
    { }
    ATTACH_OPERATIONS()
  };

  ///////////////////
  // Feature queries.
  ///////////////////
  class Supports_Query : public Expression, public Vectorized<Supports_Condition*> {
  public:
    Supports_Query(ParserState pstate, size_t s = 0)
    : Expression(pstate), Vectorized<Supports_Condition*>(s)
    { }
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////
  // Feature expressions (for use inside feature queries).
  ////////////////////////////////////////////////////////
  class Supports_Condition : public Expression, public Vectorized<Supports_Condition*> {
  public:
    enum Operand { NONE, AND, OR, NOT };
  private:
    ADD_PROPERTY(String*, feature)
    ADD_PROPERTY(Expression*, value)
    ADD_PROPERTY(Operand, operand)
    ADD_PROPERTY(bool, is_root)
  public:
    Supports_Condition(ParserState pstate, size_t s = 0, String* f = 0,
                            Expression* v = 0, Operand o = NONE, bool r = false)
    : Expression(pstate), Vectorized<Supports_Condition*>(s),
      feature_(f), value_(v), operand_(o), is_root_(r)
    { }
    ATTACH_OPERATIONS()
  };

  /////////////////////////////////////////////////
  // At root expressions (for use inside @at-root).
  /////////////////////////////////////////////////
  class At_Root_Expression : public Expression {
  private:
    ADD_PROPERTY(String*, feature)
    ADD_PROPERTY(Expression*, value)
    ADD_PROPERTY(bool, is_interpolated)
  public:
    At_Root_Expression(ParserState pstate, String* f = 0, Expression* v = 0, bool i = false)
    : Expression(pstate), feature_(f), value_(v), is_interpolated_(i)
    { }
    bool exclude(string str)
    {
      To_String to_string;
      bool with = feature() && unquote(feature()->perform(&to_string)).compare("with") == 0;
      List* l = static_cast<List*>(value());
      string v;

      if (with)
      {
        if (!l || l->length() == 0) return str.compare("rule") != 0;
        for (size_t i = 0, L = l->length(); i < L; ++i)
        {
          v = unquote((*l)[i]->perform(&to_string));
          if (v.compare("all") == 0 || v == str) return false;
        }
        return true;
      }
      else
      {
        if (!l || !l->length()) return str.compare("rule") == 0;
        for (size_t i = 0, L = l->length(); i < L; ++i)
        {
          v = unquote((*l)[i]->perform(&to_string));
          if (v.compare("all") == 0 || v == str) return true;
        }
        return false;
      }
    }
    ATTACH_OPERATIONS()
  };

  ///////////
  // At-root.
  ///////////
  class At_Root_Block : public Has_Block {
    ADD_PROPERTY(At_Root_Expression*, expression)
  public:
    At_Root_Block(ParserState pstate, Block* b = 0, At_Root_Expression* e = 0)
    : Has_Block(pstate, b), expression_(e)
    { statement_type(ATROOT); }
    bool is_hoistable() { return true; }
    bool bubbles() { return true; }
    bool exclude_node(Statement* s) {
      if (s->statement_type() == Statement::DIRECTIVE)
      {
        return expression()->exclude(static_cast<At_Rule*>(s)->keyword().erase(0, 1));
      }
      if (s->statement_type() == Statement::MEDIA)
      {
        return expression()->exclude("media");
      }
      if (s->statement_type() == Statement::RULESET)
      {
        return expression()->exclude("rule");
      }
      if (s->statement_type() == Statement::SUPPORTS)
      {
        return expression()->exclude("supports");
      }
      if (static_cast<At_Rule*>(s)->is_keyframes())
      {
        return expression()->exclude("keyframes");
      }
      return false;
    }
    ATTACH_OPERATIONS()
  };

  //////////////////
  // The null value.
  //////////////////
  class Null : public Value {
  public:
    Null(ParserState pstate) : Value(pstate) { concrete_type(NULL_VAL); }
    string type() { return "null"; }
    static string type_name() { return "null"; }
    bool is_invisible() const { return true; }
    operator bool() { return false; }
    bool is_false() { return true; }

    virtual size_t hash()
    {
      return -1;
    }

    virtual bool operator== (const Expression& rhs) const;
    virtual bool operator== (const Expression* rhs) const;
    virtual string to_string(bool compressed = false, int precision = 5) const;

    ATTACH_OPERATIONS()
  };

  /////////////////////////////////
  // Thunks for delayed evaluation.
  /////////////////////////////////
  class Thunk : public Expression {
    ADD_PROPERTY(Expression*, expression)
    ADD_PROPERTY(Env*, environment)
  public:
    Thunk(ParserState pstate, Expression* exp, Env* env = 0)
    : Expression(pstate), expression_(exp), environment_(env)
    { }
  };

  /////////////////////////////////////////////////////////
  // Individual parameter objects for mixins and functions.
  /////////////////////////////////////////////////////////
  class Parameter : public AST_Node {
    ADD_PROPERTY(string, name)
    ADD_PROPERTY(Expression*, default_value)
    ADD_PROPERTY(bool, is_rest_parameter)
  public:
    Parameter(ParserState pstate,
              string n, Expression* def = 0, bool rest = false)
    : AST_Node(pstate), name_(n), default_value_(def), is_rest_parameter_(rest)
    {
      if (default_value_ && is_rest_parameter_) {
        error("variable-length parameter may not have a default value", pstate);
      }
    }
    ATTACH_OPERATIONS()
  };

  /////////////////////////////////////////////////////////////////////////
  // Parameter lists -- in their own class to facilitate context-sensitive
  // error checking (e.g., ensuring that all optional parameters follow all
  // required parameters).
  /////////////////////////////////////////////////////////////////////////
  class Parameters : public AST_Node, public Vectorized<Parameter*> {
    ADD_PROPERTY(bool, has_optional_parameters)
    ADD_PROPERTY(bool, has_rest_parameter)
  protected:
    void adjust_after_pushing(Parameter* p)
    {
      if (p->default_value()) {
        if (has_rest_parameter_) {
          error("optional parameters may not be combined with variable-length parameters", p->pstate());
        }
        has_optional_parameters_ = true;
      }
      else if (p->is_rest_parameter()) {
        if (has_rest_parameter_) {
          error("functions and mixins cannot have more than one variable-length parameter", p->pstate());
        }
        has_rest_parameter_ = true;
      }
      else {
        if (has_rest_parameter_) {
          error("required parameters must precede variable-length parameters", p->pstate());
        }
        if (has_optional_parameters_) {
          error("required parameters must precede optional parameters", p->pstate());
        }
      }
    }
  public:
    Parameters(ParserState pstate)
    : AST_Node(pstate),
      Vectorized<Parameter*>(),
      has_optional_parameters_(false),
      has_rest_parameter_(false)
    { }
    ATTACH_OPERATIONS()
  };

  //////////////////////////////////////////////////////////////////////////////////////////
  // Additional method on Lists to retrieve values directly or from an encompassed Argument.
  //////////////////////////////////////////////////////////////////////////////////////////
  inline Expression* List::value_at_index(size_t i) { return is_arglist_ ? ((Argument*)(*this)[i])->value() : (*this)[i]; }

  /////////////////////////////////////////
  // Abstract base class for CSS selectors.
  /////////////////////////////////////////
  class Selector : public Expression {
    ADD_PROPERTY(bool, has_reference)
    ADD_PROPERTY(bool, has_placeholder)
    // line break before list separator
    ADD_PROPERTY(bool, has_line_feed)
    // line break after list separator
    ADD_PROPERTY(bool, has_line_break)
    // maybe we have optional flag
    ADD_PROPERTY(bool, is_optional)
    // parent block pointers
    ADD_PROPERTY(Media_Block*, media_block)
  public:
    Selector(ParserState pstate, bool r = false, bool h = false)
    : Expression(pstate),
      has_reference_(r),
      has_placeholder_(h),
      has_line_feed_(false),
      has_line_break_(false),
      is_optional_(false),
      media_block_(0)
    { concrete_type(SELECTOR); }
    virtual ~Selector() = 0;
    virtual unsigned long specificity() {
      return Constants::Specificity_Universal;
    }
  };
  inline Selector::~Selector() { }

  /////////////////////////////////////////////////////////////////////////
  // Interpolated selectors -- the interpolated String will be expanded and
  // re-parsed into a normal selector class.
  /////////////////////////////////////////////////////////////////////////
  class Selector_Schema : public Selector {
    ADD_PROPERTY(String*, contents)
    ADD_PROPERTY(bool, at_root);
  public:
    Selector_Schema(ParserState pstate, String* c)
    : Selector(pstate), contents_(c), at_root_(false)
    { }
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////
  // Abstract base class for simple selectors.
  ////////////////////////////////////////////
  class Simple_Selector : public Selector {
    ADD_PROPERTY(string, ns);
    ADD_PROPERTY(string, name)
    ADD_PROPERTY(bool, has_ns)
  public:
    Simple_Selector(ParserState pstate, string n = "")
    : Selector(pstate), ns_(""), name_(n), has_ns_(false)
    {
      size_t pos = n.find('|');
      // found some namespace
      if (pos != string::npos) {
        has_ns_ = true;
        ns_ = n.substr(0, pos);
        name_ = n.substr(pos + 1);
      }
    }
    virtual string ns_name() const
    {
      string name("");
      if (has_ns_)
        name += ns_ + "|";
      return name + name_;
    }
    // namespace query functions
    bool is_universal_ns() const
    {
      return has_ns_ && ns_ == "*";
    }
    bool has_universal_ns() const
    {
      return !has_ns_ || ns_ == "*";
    }
    bool is_empty_ns() const
    {
      return !has_ns_ || ns_ == "";
    }
    bool has_empty_ns() const
    {
      return has_ns_ && ns_ == "";
    }
    bool has_qualified_ns() const
    {
      return has_ns_ && ns_ != "" && ns_ != "*";
    }
    // name query functions
    bool is_universal() const
    {
      return name_ == "*";
    }

    virtual ~Simple_Selector() = 0;
    virtual Compound_Selector* unify_with(Compound_Selector*, Context&);
    virtual bool has_parent_ref() { return false; };
    virtual bool is_pseudo_element() { return false; }
    virtual bool is_pseudo_class() { return false; }

    virtual bool is_superselector_of(Compound_Selector* sub) { return false; }

    bool operator==(const Simple_Selector& rhs) const;
    inline bool operator!=(const Simple_Selector& rhs) const { return !(*this == rhs); }

    bool operator<(const Simple_Selector& rhs) const;
    ATTACH_OPERATIONS();
  };
  inline Simple_Selector::~Simple_Selector() { }


  //////////////////////////////////
  // The Parent Selector Expression.
  //////////////////////////////////
  // parent selectors can occur in selectors but also
  // inside strings in declarations (Compound_Selector).
  // only one simple parent selector means the first case.
  class Parent_Selector : public Simple_Selector {
  public:
    Parent_Selector(ParserState pstate)
    : Simple_Selector(pstate, "&")
    { has_reference(true); }
    virtual bool has_parent_ref() { return true; };
    virtual unsigned long specificity()
    {
      return 0;
    }
    string type() { return "selector"; }
    static string type_name() { return "selector"; }
    ATTACH_OPERATIONS()
  };

  /////////////////////////////////////////////////////////////////////////
  // Placeholder selectors (e.g., "%foo") for use in extend-only selectors.
  /////////////////////////////////////////////////////////////////////////
  class Selector_Placeholder : public Simple_Selector {
  public:
    Selector_Placeholder(ParserState pstate, string n)
    : Simple_Selector(pstate, n)
    { has_placeholder(true); }
    // virtual Selector_Placeholder* find_placeholder();
    ATTACH_OPERATIONS()
  };

  /////////////////////////////////////////////////////////////////////
  // Type selectors (and the universal selector) -- e.g., div, span, *.
  /////////////////////////////////////////////////////////////////////
  class Type_Selector : public Simple_Selector {
  public:
    Type_Selector(ParserState pstate, string n)
    : Simple_Selector(pstate, n)
    { }
    virtual unsigned long specificity()
    {
      // ToDo: What is the specificity of the star selector?
      if (name() == "*") return Constants::Specificity_Universal;
      else               return Constants::Specificity_Type;
    }
    virtual Simple_Selector* unify_with(Simple_Selector*, Context&);
    virtual Compound_Selector* unify_with(Compound_Selector*, Context&);
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////
  // Selector qualifiers -- i.e., classes and ids.
  ////////////////////////////////////////////////
  class Selector_Qualifier : public Simple_Selector {
  public:
    Selector_Qualifier(ParserState pstate, string n)
    : Simple_Selector(pstate, n)
    { }
    virtual unsigned long specificity()
    {
      if (name()[0] == '#') return Constants::Specificity_ID;
      if (name()[0] == '.') return Constants::Specificity_Class;
      else                  return Constants::Specificity_Type;
    }
    virtual Compound_Selector* unify_with(Compound_Selector*, Context&);
    ATTACH_OPERATIONS()
  };

  ///////////////////////////////////////////////////
  // Attribute selectors -- e.g., [src*=".jpg"], etc.
  ///////////////////////////////////////////////////
  class Attribute_Selector : public Simple_Selector {
    ADD_PROPERTY(string, matcher)
    ADD_PROPERTY(String*, value) // might be interpolated
  public:
    Attribute_Selector(ParserState pstate, string n, string m, String* v)
    : Simple_Selector(pstate, n), matcher_(m), value_(v)
    { }
    virtual unsigned long specificity()
    {
      return Constants::Specificity_Attr;
    }
    ATTACH_OPERATIONS()
  };

  //////////////////////////////////////////////////////////////////
  // Pseudo selectors -- e.g., :first-child, :nth-of-type(...), etc.
  //////////////////////////////////////////////////////////////////
  /* '::' starts a pseudo-element, ':' a pseudo-class */
  /* Except :first-line, :first-letter, :before and :after */
  /* Note that pseudo-elements are restricted to one per selector */
  /* and occur only in the last simple_selector_sequence. */
  inline bool is_pseudo_class_element(const string& name)
  {
    return name == ":before"       ||
           name == ":after"        ||
           name == ":first-line"   ||
           name == ":first-letter";
  }

  class Pseudo_Selector : public Simple_Selector {
    ADD_PROPERTY(String*, expression)
  public:
    Pseudo_Selector(ParserState pstate, string n, String* expr = 0)
    : Simple_Selector(pstate, n), expression_(expr)
    { }

    // A pseudo-class always consists of a "colon" (:) followed by the name
    // of the pseudo-class and optionally by a value between parentheses.
    virtual bool is_pseudo_class()
    {
      return (name_[0] == ':' && name_[1] != ':')
             && ! is_pseudo_class_element(name_);
    }

    // A pseudo-element is made of two colons (::) followed by the name.
    // The `::` notation is introduced by the current document in order to
    // establish a discrimination between pseudo-classes and pseudo-elements.
    // For compatibility with existing style sheets, user agents must also
    // accept the previous one-colon notation for pseudo-elements introduced
    // in CSS levels 1 and 2 (namely, :first-line, :first-letter, :before and
    // :after). This compatibility is not allowed for the new pseudo-elements
    // introduced in this specification.
    virtual bool is_pseudo_element()
    {
      return (name_[0] == ':' && name_[1] == ':')
             || is_pseudo_class_element(name_);
    }
    virtual unsigned long specificity()
    {
      if (is_pseudo_element())
        return Constants::Specificity_Type;
      return Constants::Specificity_Pseudo;
    }
    virtual Compound_Selector* unify_with(Compound_Selector*, Context&);
    ATTACH_OPERATIONS()
  };

  /////////////////////////////////////////////////
  // Wrapped selector -- pseudo selector that takes a list of selectors as argument(s) e.g., :not(:first-of-type), :-moz-any(ol p.blah, ul, menu, dir)
  /////////////////////////////////////////////////
  class Wrapped_Selector : public Simple_Selector {
    ADD_PROPERTY(Selector*, selector)
  public:
    Wrapped_Selector(ParserState pstate, string n, Selector* sel)
    : Simple_Selector(pstate, n), selector_(sel)
    { }
    virtual bool is_superselector_of(Wrapped_Selector* sub);
    // Selectors inside the negation pseudo-class are counted like any
    // other, but the negation itself does not count as a pseudo-class.
    virtual unsigned long specificity()
    {
      return selector_ ? selector_->specificity() : 0;
    }
    bool operator==(const Simple_Selector& rhs) const;
    bool operator==(const Wrapped_Selector& rhs) const;
    ATTACH_OPERATIONS()
  };

  struct Complex_Selector_Pointer_Compare {
    bool operator() (const Complex_Selector* const pLeft, const Complex_Selector* const pRight) const;
  };

  ////////////////////////////////////////////////////////////////////////////
  // Simple selector sequences. Maintains flags indicating whether it contains
  // any parent references or placeholders, to simplify expansion.
  ////////////////////////////////////////////////////////////////////////////
  typedef set<Complex_Selector*, Complex_Selector_Pointer_Compare> SourcesSet;
  class Compound_Selector : public Selector, public Vectorized<Simple_Selector*> {
  private:
    SourcesSet sources_;
    ADD_PROPERTY(bool, has_parent_reference);
  protected:
    void adjust_after_pushing(Simple_Selector* s)
    {
      if (s->has_reference())   has_reference(true);
      if (s->has_placeholder()) has_placeholder(true);
    }
  public:
    Compound_Selector(ParserState pstate, size_t s = 0)
    : Selector(pstate),
      Vectorized<Simple_Selector*>(s),
      has_parent_reference_(false)
    { }
    bool contains_placeholder() {
      for (size_t i = 0, L = length(); i < L; ++i) {
        if ((*this)[i]->has_placeholder()) return true;
      }
      return false;
    };

    bool is_universal() const
    {
      return length() == 1 && (*this)[0]->is_universal();
    }

    Complex_Selector* to_complex(Memory_Manager<AST_Node>& mem);
    Compound_Selector* unify_with(Compound_Selector* rhs, Context& ctx);
    // virtual Selector_Placeholder* find_placeholder();
    virtual bool has_parent_ref();
    Simple_Selector* base()
    {
      // Implement non-const in terms of const. Safe to const_cast since this method is non-const
      return const_cast<Simple_Selector*>(static_cast<const Compound_Selector*>(this)->base());
    }
    const Simple_Selector* base() const {
      if (length() == 0) return 0;
      if (typeid(*(*this)[0]) == typeid(Type_Selector))
        return (*this)[0];
//      else cerr << "SERIOUSELY " << "\n";
      return 0;
    }
    virtual bool is_superselector_of(Compound_Selector* sub, string wrapped = "");
    virtual bool is_superselector_of(Complex_Selector* sub, string wrapped = "");
    virtual bool is_superselector_of(Selector_List* sub, string wrapped = "");
    virtual unsigned long specificity()
    {
      int sum = 0;
      for (size_t i = 0, L = length(); i < L; ++i)
      { sum += (*this)[i]->specificity(); }
      return sum;
    }
    bool is_empty_reference()
    {
      return length() == 1 &&
             typeid(*(*this)[0]) == typeid(Parent_Selector);
    }
    vector<string> to_str_vec(); // sometimes need to convert to a flat "by-value" data structure

    bool operator<(const Compound_Selector& rhs) const;

    bool operator==(const Compound_Selector& rhs) const;
    inline bool operator!=(const Compound_Selector& rhs) const { return !(*this == rhs); }

    SourcesSet& sources() { return sources_; }
    void clearSources() { sources_.clear(); }
    void mergeSources(SourcesSet& sources, Context& ctx);

    Compound_Selector* clone(Context&) const; // does not clone the Simple_Selector*s

    Compound_Selector* minus(Compound_Selector* rhs, Context& ctx);
    ATTACH_OPERATIONS()
  };

  ////////////////////////////////////////////////////////////////////////////
  // General selectors -- i.e., simple sequences combined with one of the four
  // CSS selector combinators (">", "+", "~", and whitespace). Essentially a
  // linked list.
  ////////////////////////////////////////////////////////////////////////////
  class Complex_Selector : public Selector {
  public:
    enum Combinator { ANCESTOR_OF, PARENT_OF, PRECEDES, ADJACENT_TO, REFERENCE };
  private:
    ADD_PROPERTY(Combinator, combinator)
    ADD_PROPERTY(Compound_Selector*, head)
    ADD_PROPERTY(Complex_Selector*, tail)
    ADD_PROPERTY(String*, reference);
  public:
    bool contains_placeholder() {
      if (head() && head()->contains_placeholder()) return true;
      if (tail() && tail()->contains_placeholder()) return true;
      return false;
    };
    Complex_Selector(ParserState pstate,
                     Combinator c = ANCESTOR_OF,
                     Compound_Selector* h = 0,
                     Complex_Selector* t = 0)
    : Selector(pstate), combinator_(c), head_(h), tail_(t), reference_(0)
    {
      if ((h && h->has_reference())   || (t && t->has_reference()))   has_reference(true);
      if ((h && h->has_placeholder()) || (t && t->has_placeholder())) has_placeholder(true);
    }
    virtual bool has_parent_ref();

    // can still have a tail
    bool is_empty_ancestor() const
    {
      return (!head() || head()->length() == 0) &&
             combinator() == Combinator::ANCESTOR_OF;
    }

    Complex_Selector* context(Context&);


    // front returns the first real tail
    // skips over parent and empty ones
    const Complex_Selector* first() const;

    // last returns the last real tail
    const Complex_Selector* last() const;

    // unconstant accessors
    Complex_Selector* first();
    Complex_Selector* last();

    // some shortcuts that should be removed
    const Complex_Selector* innermost() const { return last(); };
    Complex_Selector* innermost() { return last(); };

    size_t length() const;
    Complex_Selector* parentize(Context& ctx);
    Selector_List* parentize(Selector_List* parents, Context& ctx);
    Complex_Selector* parentize(Complex_Selector* parent, Context& ctx);
    virtual bool is_superselector_of(Compound_Selector* sub, string wrapping = "");
    virtual bool is_superselector_of(Complex_Selector* sub, string wrapping = "");
    virtual bool is_superselector_of(Selector_List* sub, string wrapping = "");
    // virtual Selector_Placeholder* find_placeholder();
    Selector_List* unify_with(Complex_Selector* rhs, Context& ctx);
    Combinator clear_innermost();
    void set_innermost(Complex_Selector*, Combinator);
    virtual unsigned long specificity() const
    {
      int sum = 0;
      if (head()) sum += head()->specificity();
      if (tail()) sum += tail()->specificity();
      return sum;
    }
    bool operator<(const Complex_Selector& rhs) const;
    bool operator==(const Complex_Selector& rhs) const;
    inline bool operator!=(const Complex_Selector& rhs) const { return !(*this == rhs); }
    SourcesSet sources()
    {
      //s = Set.new
      //seq.map {|sseq_or_op| s.merge sseq_or_op.sources if sseq_or_op.is_a?(SimpleSequence)}
      //s

      SourcesSet srcs;

      Compound_Selector* pHead = head();
      Complex_Selector*  pTail = tail();

      if (pHead) {
        SourcesSet& headSources = pHead->sources();
        srcs.insert(headSources.begin(), headSources.end());
      }

      if (pTail) {
        SourcesSet tailSources = pTail->sources();
        srcs.insert(tailSources.begin(), tailSources.end());
      }

      return srcs;
    }
    void addSources(SourcesSet& sources, Context& ctx) {
      // members.map! {|m| m.is_a?(SimpleSequence) ? m.with_more_sources(sources) : m}
      Complex_Selector* pIter = this;
      while (pIter) {
        Compound_Selector* pHead = pIter->head();

        if (pHead) {
          pHead->mergeSources(sources, ctx);
        }

        pIter = pIter->tail();
      }
    }
    void clearSources() {
      Complex_Selector* pIter = this;
      while (pIter) {
        Compound_Selector* pHead = pIter->head();

        if (pHead) {
          pHead->clearSources();
        }

        pIter = pIter->tail();
      }
    }
    Complex_Selector* clone(Context&) const;      // does not clone Compound_Selector*s
    Complex_Selector* cloneFully(Context&) const; // clones Compound_Selector*s
    // vector<Compound_Selector*> to_vector();
    ATTACH_OPERATIONS()
  };

  typedef deque<Complex_Selector*> ComplexSelectorDeque;
  typedef Subset_Map<string, pair<Complex_Selector*, Compound_Selector*> > ExtensionSubsetMap;

  ///////////////////////////////////
  // Comma-separated selector groups.
  ///////////////////////////////////
  class Selector_List : public Selector, public Vectorized<Complex_Selector*> {
#ifdef DEBUG
    ADD_PROPERTY(string, mCachedSelector)
#endif
    ADD_PROPERTY(vector<string>, wspace)
  protected:
    void adjust_after_pushing(Complex_Selector* c);
  public:
    Selector_List(ParserState pstate, size_t s = 0)
    : Selector(pstate), Vectorized<Complex_Selector*>(s), wspace_(0)
    { }
    // remove parent selector references
    // basically unwraps parsed selectors
    void remove_parent_selectors();
    // virtual Selector_Placeholder* find_placeholder();
    Selector_List* parentize(Context& ctx);
    Selector_List* parentize(Selector_List* parents, Context& ctx);
    Selector_List* parentize(Complex_Selector* parent, Context& ctx);
    virtual bool is_superselector_of(Compound_Selector* sub, string wrapping = "");
    virtual bool is_superselector_of(Complex_Selector* sub, string wrapping = "");
    virtual bool is_superselector_of(Selector_List* sub, string wrapping = "");
    Selector_List* unify_with(Selector_List*, Context&);
    void populate_extends(Selector_List*, Context&, ExtensionSubsetMap&);
    virtual unsigned long specificity()
    {
      unsigned long sum = 0;
      unsigned long specificity = 0;
      for (size_t i = 0, L = length(); i < L; ++i)
      {
        specificity = (*this)[i]->specificity();
        if (sum < specificity) sum = specificity;
      }
      return sum;
    }
    Selector_List* clone(Context&) const;      // does not clone Compound_Selector*s
    Selector_List* cloneFully(Context&) const; // clones Compound_Selector*s
    virtual bool operator==(const Selector& rhs) const;
    virtual bool operator==(const Selector_List& rhs) const;
    ATTACH_OPERATIONS()
  };

  inline bool Ruleset::is_invisible() const {
    bool is_invisible = true;
    Selector_List* sl = static_cast<Selector_List*>(selector());
    for (size_t i = 0, L = sl->length(); i < L && is_invisible; ++i)
      is_invisible &= (*sl)[i]->has_placeholder();
    return is_invisible;
  }


  template<typename SelectorType>
  bool selectors_equal(const SelectorType& one, const SelectorType& two, bool simpleSelectorOrderDependent) {
    // Test for equality among selectors while differentiating between checks that demand the underlying Simple_Selector
    // ordering to be the same or not. This works because operator< (which doesn't make a whole lot of sense for selectors, but
    // is required for proper stl collection ordering) is implemented using string comparision. This gives stable sorting
    // behavior, and can be used to determine if the selectors would have exactly idential output. operator== matches the
    // ruby sass implementations for eql, which sometimes perform order independent comparisions (like set comparisons of the
    // members of a SimpleSequence (Compound_Selector)).
    //
    // Due to the reliance on operator== and operater< behavior, this templated method is currently only intended for
    // use with Compound_Selector and Complex_Selector objects.
    if (simpleSelectorOrderDependent) {
      return !(one < two) && !(two < one);
    } else {
      return one == two;
    }
  }

  // compare function for sorting and probably other other uses
  struct cmp_complex_selector { inline bool operator() (const Complex_Selector* l, const Complex_Selector* r) { return (*l < *r); } };
  struct cmp_compound_selector { inline bool operator() (const Compound_Selector* l, const Compound_Selector* r) { return (*l < *r); } };
  struct cmp_simple_selector { inline bool operator() (const Simple_Selector* l, const Simple_Selector* r) { return (*l < *r); } };

}

#ifdef __clang__

#pragma clang diagnostic pop

#endif

#endif
