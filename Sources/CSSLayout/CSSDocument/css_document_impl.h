/*
**  ClanLib SDK
**  Copyright (c) 1997-2012 The ClanLib Team
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
**  Note: Some of the libraries ClanLib may link to may have additional
**  requirements or restrictions.
**
**  File Author(s):
**
**    Magnus Norddahl
*/

#pragma once

#include "API/CSSLayout/css_property.h"
#include "API/CSSLayout/css_property_list.h"
#include "API/CSSLayout/css_select_node.h"
#include "API/CSSLayout/dom_select_node.h"
#include "css_ruleset.h"
#include "css_selector_chain.h"
#include "css_selector_link.h"
#include "css_ruleset_match.h"
#include <algorithm>

namespace clan
{

class CSSDocument_Impl
{
public:
	CSSDocument_Impl() : next_origin(0) { }
	std::vector<CSSRulesetMatch> select_rulesets(CSSSelectNode *node, const std::string &pseudo_element);
	bool try_match_chain(const CSSSelectorChain &chain, CSSSelectNode *node, size_t chain_index);
	bool try_match_link(const CSSSelectorLink &link, CSSSelectNode *node);
	void read_stylesheet(CSSTokenizer &tokenizer, const std::string &base_uri);
	void read_at_rule(CSSTokenizer &tokenizer, CSSToken &token);
	void read_statement(CSSTokenizer &tokenizer, CSSToken &token);
	void read_end_of_at_rule(CSSTokenizer &tokenizer, CSSToken &token);
	static bool read_end_of_statement(CSSTokenizer &tokenizer, CSSToken &token);
	bool read_selector_chain(CSSTokenizer &tokenizer, CSSToken &token, CSSSelectorChain &out_selector_chain);
	static bool read_property_value(CSSTokenizer &tokenizer, CSSToken &token, CSSProperty &property, std::string base_uri);
	std::string to_string(const CSSToken &token);
	static bool equals(const std::string &s1, const std::string &s2);
	static std::string make_absolute_uri(std::string uri, std::string base_uri);

	std::string base_uri;
	std::vector<CSSRuleset> rulesets;
	int next_origin;
};

}
