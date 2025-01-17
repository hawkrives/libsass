#include "node.hpp"
#include "to_string.hpp"
#include "context.hpp"
#include "parser.hpp"

namespace Sass {


  Node Node::createCombinator(const Complex_Selector::Combinator& combinator) {
    NodeDequePtr null;
    return Node(COMBINATOR, combinator, NULL /*pSelector*/, null /*pCollection*/);
  }


  Node Node::createSelector(Complex_Selector* pSelector, Context& ctx) {
    NodeDequePtr null;

    Complex_Selector* pStripped = pSelector->clone(ctx);
    pStripped->tail(NULL);
    pStripped->combinator(Complex_Selector::ANCESTOR_OF);

    Node n(SELECTOR, Complex_Selector::ANCESTOR_OF, pStripped, null /*pCollection*/);
    if (pSelector) n.got_line_feed = pSelector->has_line_feed();
    return n;
  }


  Node Node::createCollection() {
    NodeDequePtr pEmptyCollection = make_shared<NodeDeque>();
    return Node(COLLECTION, Complex_Selector::ANCESTOR_OF, NULL /*pSelector*/, pEmptyCollection);
  }


  Node Node::createCollection(const NodeDeque& values) {
    NodeDequePtr pShallowCopiedCollection = make_shared<NodeDeque>(values);
    return Node(COLLECTION, Complex_Selector::ANCESTOR_OF, NULL /*pSelector*/, pShallowCopiedCollection);
  }


  Node Node::createNil() {
    NodeDequePtr null;
    return Node(NIL, Complex_Selector::ANCESTOR_OF, NULL /*pSelector*/, null /*pCollection*/);
  }


  Node::Node(const TYPE& type, Complex_Selector::Combinator combinator, Complex_Selector* pSelector, NodeDequePtr& pCollection)
  : got_line_feed(false), mType(type), mCombinator(combinator), mpSelector(pSelector), mpCollection(pCollection)
  { if (pSelector) got_line_feed = pSelector->has_line_feed(); }


  Node Node::clone(Context& ctx) const {
    NodeDequePtr pNewCollection = make_shared<NodeDeque>();
    if (mpCollection) {
      for (NodeDeque::iterator iter = mpCollection->begin(), iterEnd = mpCollection->end(); iter != iterEnd; iter++) {
        Node& toClone = *iter;
        pNewCollection->push_back(toClone.clone(ctx));
      }
    }

    Node n(mType, mCombinator, mpSelector ? mpSelector->clone(ctx) : NULL, pNewCollection);
    n.got_line_feed = got_line_feed;
    return n;
  }


  bool Node::contains(const Node& potentialChild, bool simpleSelectorOrderDependent) const {
  	bool found = false;

    for (NodeDeque::iterator iter = mpCollection->begin(), iterEnd = mpCollection->end(); iter != iterEnd; iter++) {
      Node& toTest = *iter;

      if (nodesEqual(toTest, potentialChild, simpleSelectorOrderDependent)) {
        found = true;
        break;
      }
    }

    return found;
  }


  bool Node::operator==(const Node& rhs) const {
  	return nodesEqual(*this, rhs, true /*simpleSelectorOrderDependent*/);
  }


  bool nodesEqual(const Node& lhs, const Node& rhs, bool simpleSelectorOrderDependent) {
    if (lhs.type() != rhs.type()) {
      return false;
    }

    if (lhs.isCombinator()) {

    	return lhs.combinator() == rhs.combinator();

    } else if (lhs.isNil()) {

      return true; // no state to check

    } else if (lhs.isSelector()){

      return selectors_equal(*lhs.selector(), *rhs.selector(), simpleSelectorOrderDependent);

    } else if (lhs.isCollection()) {

      if (lhs.collection()->size() != rhs.collection()->size()) {
        return false;
      }

      for (NodeDeque::iterator lhsIter = lhs.collection()->begin(), lhsIterEnd = lhs.collection()->end(),
           rhsIter = rhs.collection()->begin(); lhsIter != lhsIterEnd; lhsIter++, rhsIter++) {

        if (!nodesEqual(*lhsIter, *rhsIter, simpleSelectorOrderDependent)) {
          return false;
        }

      }

      return true;

    }

    // We shouldn't get here.
    throw "Comparing unknown node types. A new type was probably added and this method wasn't implemented for it.";
  }


  void Node::plus(Node& rhs) {
  	if (!this->isCollection() || !rhs.isCollection()) {
    	throw "Both the current node and rhs must be collections.";
    }
  	this->collection()->insert(this->collection()->end(), rhs.collection()->begin(), rhs.collection()->end());
  }

#ifdef DEBUG
  ostream& operator<<(ostream& os, const Node& node) {

    if (node.isCombinator()) {

      switch (node.combinator()) {
        case Complex_Selector::ANCESTOR_OF: os << "\" \""; break;
        case Complex_Selector::PARENT_OF:   os << "\">\""; break;
        case Complex_Selector::PRECEDES:    os << "\"~\""; break;
        case Complex_Selector::ADJACENT_TO: os << "\"+\""; break;
        case Complex_Selector::REFERENCE: os    << "\"/\""; break;
      }

    } else if (node.isNil()) {

      os << "nil";

    } else if (node.isSelector()){

      To_String to_string;
      os << node.selector()->head()->perform(&to_string);

    } else if (node.isCollection()) {

      os << "[";

      for (NodeDeque::iterator iter = node.collection()->begin(), iterBegin = node.collection()->begin(), iterEnd = node.collection()->end(); iter != iterEnd; iter++) {
        if (iter != iterBegin) {
          os << ", ";
        }

        os << (*iter);
      }

      os << "]";

    }

    return os;

  }
#endif


  Node complexSelectorToNode(Complex_Selector* pToConvert, Context& ctx) {
    if (pToConvert == NULL) {
      return Node::createNil();
    }
    Node node = Node::createCollection();
    node.got_line_feed = pToConvert->has_line_feed();
    bool has_lf = pToConvert->has_line_feed();

    // unwrap the selector from parent ref
    if (pToConvert->head() && pToConvert->head()->has_parent_ref()) {
      Complex_Selector* tail = pToConvert->tail();
      if (tail) tail->has_line_feed(pToConvert->has_line_feed());
      pToConvert = tail;
    }

    while (pToConvert) {

      bool empty_parent_ref = pToConvert->head() && pToConvert->head()->is_empty_reference();

      if (pToConvert->head() == NULL || empty_parent_ref) {
      }

      // the first Complex_Selector may contain a dummy head pointer, skip it.
      if (pToConvert->head() != NULL && !empty_parent_ref) {
        node.collection()->push_back(Node::createSelector(pToConvert, ctx));
        if (has_lf) node.collection()->back().got_line_feed = has_lf;
        has_lf = false;
      }

      if (pToConvert->combinator() != Complex_Selector::ANCESTOR_OF) {
        node.collection()->push_back(Node::createCombinator(pToConvert->combinator()));
        if (has_lf) node.collection()->back().got_line_feed = has_lf;
        has_lf = false;
      }

      if (pToConvert && empty_parent_ref && pToConvert->tail()) {
        // pToConvert->tail()->has_line_feed(pToConvert->has_line_feed());
      }

      pToConvert = pToConvert->tail();
    }

    return node;
  }


  Complex_Selector* nodeToComplexSelector(const Node& toConvert, Context& ctx) {
    if (toConvert.isNil()) {
      return NULL;
    }


    if (!toConvert.isCollection()) {
      throw "The node to convert to a Complex_Selector* must be a collection type or nil.";
    }


    NodeDeque& childNodes = *toConvert.collection();

    string noPath("");
    Position noPosition(-1, -1, -1);
    Complex_Selector* pFirst = new (ctx.mem) Complex_Selector(ParserState("[NODE]"), Complex_Selector::ANCESTOR_OF, NULL, NULL);

    Complex_Selector* pCurrent = pFirst;

    if (toConvert.isSelector()) pFirst->has_line_feed(toConvert.got_line_feed);
    if (toConvert.isCombinator()) pFirst->has_line_feed(toConvert.got_line_feed);

    for (NodeDeque::iterator childIter = childNodes.begin(), childIterEnd = childNodes.end(); childIter != childIterEnd; childIter++) {

      Node& child = *childIter;

      if (child.isSelector()) {
        pCurrent->tail(child.selector()->clone(ctx));   // JMA - need to clone the selector, because they can end up getting shared across Node collections, and can result in an infinite loop during the call to parentSuperselector()
        // if (child.got_line_feed) pCurrent->has_line_feed(child.got_line_feed);
        pCurrent = pCurrent->tail();
      } else if (child.isCombinator()) {
        pCurrent->combinator(child.combinator());
        if (child.got_line_feed) pCurrent->has_line_feed(child.got_line_feed);

        // if the next node is also a combinator, create another Complex_Selector to hold it so it doesn't replace the current combinator
        if (childIter+1 != childIterEnd) {
          Node& nextNode = *(childIter+1);
          if (nextNode.isCombinator()) {
            pCurrent->tail(new (ctx.mem) Complex_Selector(ParserState("[NODE]"), Complex_Selector::ANCESTOR_OF, NULL, NULL));
            if (nextNode.got_line_feed) pCurrent->tail()->has_line_feed(nextNode.got_line_feed);
            pCurrent = pCurrent->tail();
          }
        }
      } else {
        throw "The node to convert's children must be only combinators or selectors.";
      }
    }

    // Put the dummy Compound_Selector in the first position, for consistency with the rest of libsass
    Compound_Selector* fakeHead = new (ctx.mem) Compound_Selector(ParserState("[NODE]"), 1);
    Parent_Selector* selectorRef = new (ctx.mem) Parent_Selector(ParserState("[NODE]"));
    fakeHead->elements().push_back(selectorRef);
    if (toConvert.got_line_feed) pFirst->has_line_feed(toConvert.got_line_feed);
    // pFirst->has_line_feed(pFirst->has_line_feed() || pFirst->tail()->has_line_feed() || toConvert.got_line_feed);
    pFirst->head(fakeHead);
    return pFirst;
  }

  // A very naive trim function, which removes duplicates in a node
  // This is only used in Complex_Selector::unify_with for now, may need modifications to fit other needs
  Node Node::naiveTrim(Node& seqses, Context& ctx) {

    SourcesSet sel_set;

    vector<Node*> res;

    // Add all selectors we don't already have, everything else just add it blindly
    // We iterate from the back to the front, since in ruby we probably overwrite existing the items
    for (NodeDeque::iterator seqsesIter = seqses.collection()->end() - 1, seqsesIterEnd = seqses.collection()->begin() - 1; seqsesIter != seqsesIterEnd; --seqsesIter) {
      Node& seqs1 = *seqsesIter;
      if( seqs1.isSelector() ) {
        auto found = sel_set.find( seqs1.selector() );
        if( found == sel_set.end() ) {
          sel_set.insert(seqs1.selector());
          res.push_back(&seqs1);
        }
      } else {
        res.push_back(&seqs1);
      }
    }
    Node result = Node::createCollection();

    for (size_t i = res.size() - 1; i != string::npos; --i) {
      result.collection()->push_back(*res[i]);
    }

    return result;
  }
}
