/*
 *  Converter from MX into Sphinx source documentation formats.
 *
 *  Copyright (C) 2010 Alex Dubov <oakad@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <string>
#include <fstream>
#include <iostream>
#include <ext/rope>
#include <functional>
#include <initializer_list>

#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/variant.hpp>
#include <boost/optional.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/xpressive/regex_actions.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/iterator/iterator_traits.hpp>
#include <boost/xpressive/xpressive_static.hpp>

using namespace std;
namespace bx = boost::xpressive;
namespace bf = boost::filesystem;

//#define DEBUG_MACROS

static const unsigned int tab_size(8);

static string pad_string(unsigned int p_size)
{
	return string(p_size / tab_size, '\t')
		      + string(p_size % tab_size, ' ');
}

template <typename pair_t>
static bool is_blank(const pair_t &in)
{
	static const bx::sregex space_expr(bx::bos >> *bx::_s >> bx::eos);

	return (bx::regex_match(in.first, in.second, space_expr));
}

template <typename pair_t>
static string trim(const pair_t &in)
{
	static const bx::sregex trim_expr(
		bx::bos >> *bx::_s >> (bx::s1 = -*bx::_) >> *bx::_s >> bx::eos
	);
	bx::smatch what;

	if (bx::regex_match(in.first, in.second, what, trim_expr))
		return what.str(1);
	else
		return string();
}

const bx::sregex brace_expr(
	(~bx::after('\\') >> '{')
			  >> *(bx::by_ref(brace_expr)
			       | bx::keep(*(~(bx::set = '{','}')
					    | (bx::after('\\')
					       >> (bx::set = '{','}')))))
			  >> (~bx::after('\\') >> '}')
);

const bx::sregex paren_expr(
	(~bx::after('\\') >> '(')
			  >> *(bx::by_ref(paren_expr)
			       | bx::keep(*(~(bx::set = '(',')')
					    | (bx::after('\\')
					       >> (bx::set = '(',')')))))
			  >> (~bx::after('\\') >> ')')
);

const bx::sregex csv_char_expr(
	 (bx::after('\\') >> ',') | ~bx::as_xpr(',')
);

const bx::sregex csv_simple_expr(
	(bx::bos | (~bx::after('\\') >> ','))
	>> (bx::s1 = *csv_char_expr)
);

const bx::sregex csv_paren_expr(
	(bx::bos | (~bx::after('\\') >> ','))
	>> (bx::s1 = *(bx::keep(paren_expr) | (bx::after('\\') >> ',')
					    | ~bx::as_xpr(',')))
);

template <typename pair_t>
static void split_csv(vector<string> &out, const pair_t &in,
		      const bx::sregex &expr = csv_simple_expr)
{
	bx::sregex_iterator b(in.first, in.second, expr), e;

	for (; b != e; ++b)
		out.push_back(b->str(1));
}

template <typename pair_t>
static void parse_href(vector<string> &out, const pair_t &in)
{
	static const bx::sregex href_expr(
		bx::bos >> *bx::_s >> "<a" >> +bx::_s >> -*bx::_ >> "href"
			>> *bx::_s >> '=' >> *bx::_s
			>> ((~bx::after('\\') >> '"' >> (bx::s1 = -*bx::_)
			     >> ~bx::after('\\') >> '"')
			    | (bx::s1 = *(~bx::_s
					  | (bx::after('\\') >> bx::_s))))
			>> -*bx::_ >> '>' >> *bx::_s >> (bx::s2 = -*bx::_)
			>> *bx::_s >> "</a>" >> *bx::_
	);

	bx::smatch what;

	if (!regex_match(in.first, in.second, what, href_expr))
		out.push_back(trim(in));
	else {
		out.push_back(what.str(1));
		out.push_back(what.str(2));
	}

}

static void null_comment(function<void (const string &)>  out,
			 const vector<string> &in)
{
}

static void c_comment(function<void (const string &)>  out,
		      const vector<string> &in)
{
	if (in.size() == 1)
		out("/* " + in[0] + "*/");
	else if (in.size() > 1) {
		out("/*");
		BOOST_FOREACH(const string &s, in) {
			if (!s.empty())
				out(" * " + s);
			else
				out(" *" + s);
		}
		out(" */");
	}
}

static void null_textref(function<void (const string &)>  out,
			 const string &name, unsigned int pos)
{
}

static void c_textref(function<void (const string &)>  out,
		      const string &name, unsigned int pos)
{
	out((boost::format("#line %1% \"%2%\"") % pos % name).str());
};

typedef function<void (function<void (const string &)>,
		       const vector<string>&)> comment_formatter_t;
typedef function<void (function<void (const string &)>,
		       const string&, unsigned int)> textref_formatter_t;

static const map<string, comment_formatter_t> comment_formatters {
	{"c", c_comment},
	{"h", c_comment}
};

static const map<string, textref_formatter_t> textref_formatters {
	{"c", c_textref}
};


/* A simplistic one. */
template <typename pair_t>
static size_t find_print_length(const pair_t &in)
{
	size_t rv(0);

	for (auto b = in.first; b < in.second; ++b) {
		if (*b == '\t')
			rv += tab_size;
		else
			++rv;
	}

	return rv;
}

struct target {
	struct line_mark {
		target *t;
		__gnu_cxx::crope::iterator begin;
		__gnu_cxx::crope::iterator end;
		unsigned int src_pos;
		string padding;
		unsigned int line_num;

		line_mark(target *t_, const string &line, unsigned int src_pos_)
		: t(t_),
		  begin(t->body.mutable_end()),
		  src_pos(src_pos_),
		  padding(t->padding),
		  line_num(t->line_cnt) {
			t->add_line(line);
			end = t->body.mutable_end();
		}
	};

	struct line_ref {
		target *t;
		__gnu_cxx::crope::iterator begin;
		__gnu_cxx::crope::iterator end;
		string padding;
		string tag;
		pair <size_t, size_t> lines;
		function<string (size_t, size_t)> editor;

		line_ref(target *t_, const string &tag_,
			 function<string (size_t, size_t)> editor_,
			 size_t s, size_t e)
		: t(t_),
		  begin(t->body.mutable_end()),
		  padding(t->padding),
		  tag(tag_),
		  lines(s, e),
		  editor(editor_) {
			t->add_line(editor(s, e));
			end = t->body.mutable_end();
		}
	};

	__gnu_cxx::crope body;
	string padding;
	unsigned int line_cnt;
	vector< boost::variant<line_mark, line_ref> > line_marks;
	map<size_t, size_t> line_exp;
	comment_formatter_t comment_formatter;
	textref_formatter_t textref_formatter;

	target(const string &tag = string())
	: line_cnt(0),
	  comment_formatter(null_comment),
	  textref_formatter(null_textref) {

		auto c_iter(comment_formatters.find(tag));
		if (c_iter != comment_formatters.end())
			comment_formatter = c_iter->second;

		auto t_iter(textref_formatters.find(tag));
		if (t_iter != textref_formatters.end())
			textref_formatter = t_iter->second;
	}

	void set_indent(unsigned int indent = 0) {
		padding = pad_string(indent);
	}

	void add_line(const string &line = string()) {
		if (!line.empty()) {
			body += padding.c_str();
			body += line.c_str();
		}

		body += '\n';
		++line_cnt;
	}

	void add_line_mark(const string &line, unsigned int src_pos) {
		line_marks.push_back(line_mark(this, line, src_pos));

		line_exp.insert(make_pair(line_cnt, 1));
	}

	void add_line_ref(const string &tag,
			  function<string (size_t, size_t)> editor,
			  size_t s, size_t e) {
		line_marks.push_back(line_ref(this, tag, editor, s, e));
	}
};

struct mx_context {
	typedef function<void (mx_context*, const string &line)> tag_handler_t;
	typedef pair<string, unsigned int> macro_line_t;
	typedef list<macro_line_t> line_block_t;

	struct macro_t {
		line_block_t lines;
		string f_name;
		unsigned int b_pos, e_pos;
	};

	enum sec_level_t {
		TITLE_LVL = 0,
		MODULE_LVL,
		SECT_LVL,
		SUBSECT_LVL,
		PAR_LVL
	};

	enum tag_level_t {
		GEN_MARK_TAG = 0,
		DOC_MARK_TAG,
		SUB_BLK_TAG,
		MACRO_REF_TAG,
		MACRO_ARG_TAG,
	};

	struct in_file {
		bf::path name;
		string base_name;
		unsigned int line_cnt;
		ifstream s;

		in_file(bf::path name_)
		: name(name_),
		  base_name(bf::path(name.filename()).replace_extension()
						     .file_string()),
		  line_cnt(0),
		  s(name.file_string().c_str(), ios::binary) {}

		string location() {
			return (boost::format("%1%:%2%")
				% name.file_string() % line_cnt).str();
		}
	};

	static map<string, mx_context::tag_handler_t> mx_tags;
	static map<string, mx_context::tag_handler_t> mx_item_tags;
	static map<string, mx_context::tag_handler_t> info_formatters;
	static vector<char> sec_heads;
	static const bx::sregex doc_mark_expr, text_mark_expr, sub_blk_expr,
				macro_ref_expr, macro_arg_expr, dead_macro_expr,
				gen_mark_expr, tag_class_expr, doc_tag_expr;
	static const bx::placeholder<tag_level_t> tag_class;

	boost::ptr_vector<in_file> in;
	string ref_name;
	map<string, target> out_files;
	decltype(out_files.begin()) doc_iter, t_iter;
	stack< pair<string, tag_handler_t> > envs;
	boost::optional<tag_handler_t> auto_end;
	unsigned int end_pos;
	vector<bf::path> includes;
	set<string> defines;
	stack< pair<string, unsigned int> > prefixes;
	int min_sec_lvl, abs_sec_lvl, rel_sec_lvl;
	int image_cnt;
	bool modulename_set;
	macro_line_t saved_line;
	vector<string> info_lines, extra_lines;

	map<string, macro_t> macros;
	decltype(macros.begin()) c_macro;

	char get_level_head(sec_level_t lvl);

	void comment(const string &line);
	void info_block(const string &line);
	void end_info_block(const string &line);
	void basename(const string &line);
	void macro_def(const string &line);
	void end_macro_def(const string &tag);
	void include(const string &line);
	void author(const string &line);
	void version(const string &line);
	void date(const string &line);
	void noindent(const string &line);
	void title(const string &line);
	void module(const string &line);
	void section(const string &line);
	void subsection(const string &line);
	void paragraph(const string &line);
	void node(const string &line);
	void end(const string &line);
	void menu(const string &line);
	void end_menu(const string &line);
	void example(const string &line);
	void end_example(const string &line);
	void qtex(const string &line);
	void end_qtex(const string &line);
	void tex(const string &line);
	void iftex(const string &line);
	void end_tex(const string &line);
	void ifset(const string &line);
	void ifclear(const string &line);
	void end_if(const string &line);
	void verbatim(const string &line);
	void end_verbatim(const string &line);
	void itemize(const string &line);
	void enumerate(const string &line);
	void end_itemize(const string &line);
	void table(const string &line);
	void end_table(const string &line);
	void multitable(const string &line);
	void end_multitable(const string &line);
	void subblock(const string &line);
	void end_subblock(const string &line);
	void subblock1(const string &line);
	void end_subblock1(const string &line);
	void item(const string &line);
	void tab(const string &line);
	void item_itemize(const string &line);
	void item_table(const string &line);

	void generic_tag(const string &tag, const string &line);
	void end_generic_tag(const string &tag);

	void expand_macros(line_block_t &out, const macro_line_t &in,
			   const target &t, unsigned int indent = 0,
			   unsigned int pass = 1);
	string replace_doc_tags(const bx::smatch &what);
	string replace_text_tags(const bx::smatch &what);

	void parse_line_include(const string &line);
	void parse_line_literal(const string &line);
	void parse_line_doc(const string &line);

	void add_line_markup(const string &line);
	void add_line_text(const string &line);
	void add_line_expand(const string &line);
	void add_line_macro(const string &line);
	void add_line_menu(const string &line);
	void add_line_asis(const string &line);
	void add_line_info(const string &line);
	void add_line_noop(const string &line);

	void add_line_c_comment(const string &line);

	tag_handler_t parse_line;
	tag_handler_t add_line, add_line_prev;

	struct late_expand : public boost::static_visitor<> {
		mx_context &context;
		size_t delta;

		late_expand(mx_context &context_)
		: context(context_),
		  delta(0) {}

		void operator()(target::line_mark &m);
		void operator()(target::line_ref &m) {
			m.begin += delta;
			m.end += delta;
		}
	};

	struct ref_edit : public boost::static_visitor<> {
		mx_context &context;
		size_t delta;
		ssize_t b_delta;

		ref_edit(mx_context &context_)
		: context(context_),
		  delta(0),
		  b_delta(0){}

		void operator()(target::line_mark &m) {}
		void operator()(target::line_ref &m);
	};

	mx_context(const vector<string> &includes_, const string &doc_tag,
		   const set<string> &defines_);
	void write_out(const bf::path &prefix);
};

map<string, mx_context::tag_handler_t> mx_context::mx_tags {
	{"'", &mx_context::comment},
	{"/", &mx_context::info_block},
	{"f", &mx_context::basename},
	{"=", &mx_context::macro_def},
	{"include", &mx_context::include},
	{"a", &mx_context::author},
	{"v", &mx_context::version},
	{"d", &mx_context::date},
	{"noindent", &mx_context::noindent},
	{"t", &mx_context::title},
	{"*", &mx_context::module},
	{"+", &mx_context::section},
	{"section", &mx_context::section},
	{"subsection", &mx_context::subsection},
	{"-", &mx_context::paragraph},
	{"node", &mx_context::node},
	{"menu", &mx_context::menu},
	{"example", &mx_context::example},
	{"verbatim", &mx_context::verbatim},
	{"itemize", &mx_context::itemize},
	{"enumerate", &mx_context::enumerate},
	{"table", &mx_context::table},
	{"multitable", &mx_context::multitable},
	{"end", &mx_context::end},
	{"item", &mx_context::item},
	{"tab", &mx_context::tab},
	{"{", &mx_context::subblock},
	{"}", &mx_context::end_subblock},
	{"(", &mx_context::subblock1},
	{")", &mx_context::end_subblock1},
	{"T", &mx_context::qtex},
	{"tex", &mx_context::tex},
	{"iftex", &mx_context::iftex},
	{"ifset", &mx_context::ifset},
	{"ifclear", &mx_context::ifclear}
};

map<string, mx_context::tag_handler_t> mx_context::mx_item_tags {
	{"itemize", &mx_context::item_itemize},
	{"enumerate", &mx_context::item_itemize},
	{"table", &mx_context::item_table},
	{"multitable", &mx_context::item_table}
};

vector<char> mx_context::sec_heads{'#', '*', '=', '-', '^', '"'};

const bx::sregex mx_context::doc_mark_expr(
	'@' >> (((bx::s1 = (bx::set = '[', '%', '#', '`'))
		 >> (bx::s2 = -*bx::_) >> ~bx::after('\\')
		 >> '@' >> (bx::s4 = !bx::_d))
		| ((((bx::s1 = 'e') >> "mph")
		    | ("ci" >> (bx::s1 = 't') >> 'e')
		    | ((bx::s1 = 's') >> "trong")
		    | ((bx::s1 = 'v') >> "erb")
		    | ((bx::s1 = 'u') >> "rl")
		    | ((bx::s1 = 'i') >> "mage")
		    | ('s' >> (bx::s1 = 'c'))
		    | ('c' >> (bx::s1 = 'o') >> "de"))
		   >> (bx::s2 = brace_expr))) >> (bx::s3 = !bx::_s)
);

const bx::sregex mx_context::text_mark_expr(
	~bx::after('\\') >> '@' >> (bx::s1 = '`') >> (bx::s2 = -*bx::_)
	>> ~bx::after('\\') >> '@' >> (bx::s3 = bx::_d)
);

const bx::sregex mx_context::sub_blk_expr(
	'@' >> (bx::s1 = (bx::set = '{', '}', '(', ')'))
);

const bx::sregex mx_context::macro_ref_expr(
	"@:" >> (bx::s1 = -+bx::_)
	     >> (((bx::s2 = paren_expr) >> !(~bx::after('\\') >> '@'))
		 | ((~bx::after('\\') >> '@')
		    | (!((bx::s3 = '\\') >> bx::_s) >> bx::eos)))
);

const bx::sregex mx_context::macro_arg_expr(
	'@' >> !((bx::s3 = '?') >> '@')
	    >> ((bx::s2 = bx::_d) | ('[' >> (bx::s2 = +bx::_d) >> ']'))
);

const bx::sregex mx_context::dead_macro_expr(
	"@!!" >> (bx::s1 = -+bx::_)
	      >> ((bx::s2 = paren_expr) >> !(~bx::after('\\') >> '@')
		  | ((~bx::after('\\') >> '@') | bx::eos))
);

const bx::sregex mx_context::gen_mark_expr(
	bx::bos >> '@' >> (bx::s2 = *~bx::_s) >> *bx::_s
		>> (bx::s3 = -*bx::_) >> *bx::_s >> bx::eos
);

const bx::placeholder<mx_context::tag_level_t> mx_context::tag_class = {{}};

const bx::sregex mx_context::tag_class_expr(
	bx::bos >> *bx::_s >> ((macro_arg_expr)[tag_class = MACRO_ARG_TAG]
			       | (macro_ref_expr)[tag_class = MACRO_REF_TAG]
			       | (doc_mark_expr)[tag_class = DOC_MARK_TAG]
			       | (sub_blk_expr)[tag_class = SUB_BLK_TAG])
);

const bx::sregex mx_context::doc_tag_expr(
	macro_arg_expr | doc_mark_expr
);

char mx_context::get_level_head(sec_level_t lvl)
{
	char rv;

	if (min_sec_lvl == -1)
		min_sec_lvl = lvl;

	if (lvl > abs_sec_lvl) {
		if (rel_sec_lvl < (sec_heads.size() - 1))
			++rel_sec_lvl;
	} else if (lvl < abs_sec_lvl) {
		rel_sec_lvl -= abs_sec_lvl - lvl;
		if (rel_sec_lvl < min_sec_lvl)
			rel_sec_lvl = min_sec_lvl;
	}
	abs_sec_lvl = lvl;
	return sec_heads[rel_sec_lvl];
}

void mx_context::comment(const string &line)
{
	/* Comments are ignored */
}

void mx_context::info_block(const string &line)
{
	add_line = &mx_context::add_line_info;
	auto_end = &mx_context::end_info_block;
}

void mx_context::end_info_block(const string &line)
{
	add_line = &mx_context::add_line_markup;
	auto_end.reset();
}

void mx_context::basename(const string &line)
{
	if (!line.empty()) {
		in.back().base_name = line;
		/* sphinx will choke if there are multiple module lines */
		if ((in.size() == 1) && !modulename_set) {
			t_iter = doc_iter;
			t_iter->second.add_line(".. module:: " + line);
			t_iter->second.add_line();
			modulename_set = true;
		}
	}
}

void mx_context::macro_def(const string &line)
{
	if (line.empty())
		return;

	c_macro = macros.find(line);

	if (c_macro == macros.end())
		c_macro = macros.insert(make_pair(line, macro_t())).first;
	else {
		cerr << "macro " << line << " redefined at "
		     << in.back().location() << endl;
		c_macro->second.lines.clear();
	}

	c_macro->second.f_name = in.back().name.filename();
	c_macro->second.b_pos = in.back().line_cnt + 1;

	auto_end = &mx_context::end_macro_def;
	add_line = &mx_context::add_line_macro;
}

static void unindent_lines(mx_context::line_block_t &m)
{
	static const bx::sregex leading_space_expr(
		bx::bos >> (bx::s1 = +bx::_s)
	);
	bx::smatch what;
	size_t min_off(0);
	vector<unsigned int> pads;

	BOOST_FOREACH(auto &s, m) {
		pads.push_back(0);
		if (bx::regex_search(s.first, what, leading_space_expr)) {
			if (!what.suffix().length())
				s.first.clear();
			else {
				auto c_off(find_print_length(what[1]));

				if (min_off && (min_off > c_off))
					min_off = c_off;
				else if (!min_off)
					min_off = c_off;

				if (!min_off)
					break;

				pads.back() = c_off;
				s.first.assign(what.suffix());
			}
		} else if (!s.first.empty()) {
			min_off = 0;
			break;
		}
	}

	auto c_line(m.begin());

	BOOST_FOREACH(unsigned int pad, pads) {
		if (pad)
			c_line->first.insert(0, pad_string(pad - min_off));

		++c_line;
	}
}

void mx_context::end_macro_def(const string &line)
{
	auto_end.reset();

	if (in.size() == 1) {
		t_iter = doc_iter;

		string m_title("Macro: " + c_macro->first);
		t_iter->second.add_line();
		t_iter->second.add_line(m_title);
		t_iter->second.add_line(string(m_title.size(),
					get_level_head(SUBSECT_LVL)));
		t_iter->second.add_line(".. code-block:: guess");
		t_iter->second.add_line();
		t_iter->second.set_indent(prefixes.empty()
					  ? 3 : (prefixes.top().second + 3));

		BOOST_FOREACH(const macro_line_t &s, c_macro->second.lines)
			t_iter->second.add_line(s.first);

		t_iter->second.set_indent(prefixes.empty()
					  ? 0 : prefixes.top().second);
		t_iter->second.add_line();
		add_line = &mx_context::add_line_markup;
	} else
		add_line = &mx_context::add_line_noop;

	c_macro->second.e_pos = in.back().line_cnt + 1;
	unindent_lines(c_macro->second.lines);
	c_macro = macros.end();
}

void mx_context::include(const string &line)
{
	if (line.empty())
		return;

	BOOST_FOREACH(bf::path &p, includes) {
		bf::path f(bf::system_complete(p / line));

		if (exists(p)) {
			in.push_back(new in_file(f));

			if (in.back().s.is_open()) {
				parse_line = &mx_context::parse_line_include;
				add_line = &mx_context::add_line_noop;
				return;
			} else
				in.pop_back();
		}
	}

	cerr << "couldn't open include file " << line << " at "
	     << in.back().location() << " - skipping." << endl;
}

void mx_context::author(const string &line)
{
	if (!line.empty()) {
		t_iter = doc_iter;
		t_iter->second.add_line(":Author: " + line);
	}
}

void mx_context::version(const string &line)
{
	if (!line.empty()) {
		t_iter = doc_iter;
		t_iter->second.add_line(":Version: " + line);
	}
}

void mx_context::date(const string &line)
{
	if (!line.empty()) {
		t_iter = doc_iter;
		t_iter->second.add_line(":Date: " + line);
	}
}

void mx_context::noindent(const string &line)
{
	t_iter = doc_iter;
	t_iter->second.add_line(line);
}

void mx_context::title(const string &line)
{
	if (!line.empty()) {
		t_iter = doc_iter;
		t_iter->second.add_line();
		t_iter->second.add_line(line);
		t_iter->second.add_line(string(line.size(),
					       get_level_head(TITLE_LVL)));
		if (ref_name.empty())
			ref_name = line;
	}
}

void mx_context::module(const string &line)
{
	if (!line.empty()) {
		t_iter = doc_iter;
		t_iter->second.add_line();
		t_iter->second.add_line(line);
		t_iter->second.add_line(string(line.size(),
					       get_level_head(MODULE_LVL)));
		if (ref_name.empty())
			ref_name = line;
	}
}

void mx_context::section(const string &line)
{
	if (!line.empty()) {
		t_iter = doc_iter;
		t_iter->second.add_line();
		t_iter->second.add_line(line);
		t_iter->second.add_line(string(line.size(),
					       get_level_head(SECT_LVL)));
	}
}

void mx_context::subsection(const string &line)
{
	if (!line.empty()) {
		t_iter = doc_iter;
		t_iter->second.add_line();
		t_iter->second.add_line(line);
		t_iter->second.add_line(string(line.size(),
					       get_level_head(SUBSECT_LVL)));
	}
}

void mx_context::paragraph(const string &line)
{
	t_iter = doc_iter;
	t_iter->second.add_line();

	if (!line.empty()) {
		t_iter->second.add_line(line);
		t_iter->second.add_line(string(line.size(),
					       get_level_head(PAR_LVL)));
	} else
		t_iter->second.add_line();
}

void mx_context::node(const string &line)
{
	vector<string> nodes;
	split_csv(nodes, make_pair(line.begin(), line.end()));
	bool e_line(false);

	t_iter = doc_iter;

	BOOST_FOREACH(const string &s, nodes) {
		if (!s.empty()) {
			if (!e_line) {
				t_iter->second.add_line();
				e_line = true;
			}
			t_iter->second.add_line((boost::format(".. _%1%:")
						 % s).str());
		}
	}

	if (e_line)
		t_iter->second.add_line();
}

void mx_context::menu(const string &line)
{
	envs.push(make_pair("menu", &mx_context::end_menu));

	t_iter = doc_iter;
	t_iter->second.add_line();
	parse_line = &mx_context::parse_line_literal;
	add_line = &mx_context::add_line_menu;
}

void mx_context::end_menu(const string &line)
{
	t_iter = doc_iter;
	t_iter->second.add_line();
	t_iter->second.add_line();
	add_line = &mx_context::add_line_markup;
}

void mx_context::example(const string &line)
{
	envs.push(make_pair("example", &mx_context::end_example));

	t_iter = doc_iter;
	t_iter->second.add_line();
	t_iter->second.add_line(".. code-block:: guess");
	t_iter->second.add_line();
	parse_line = &mx_context::parse_line_literal;
	add_line = &mx_context::add_line_asis;
}

void mx_context::end_example(const string &line)
{
	t_iter = doc_iter;
	t_iter->second.add_line();
	t_iter->second.add_line();
	add_line = &mx_context::add_line_markup;
}

void mx_context::verbatim(const string &line)
{
	envs.push(make_pair("verbatim", &mx_context::end_verbatim));

	t_iter = doc_iter;
	t_iter->second.add_line("::");
	t_iter->second.add_line();
	t_iter->second.set_indent(prefixes.empty() ? 3
						   : prefixes.top().second + 3);
	parse_line = &mx_context::parse_line_literal;
	add_line = &mx_context::add_line_asis;
}

void mx_context::end_verbatim(const string &line)
{
	t_iter = doc_iter;
	t_iter->second.set_indent(prefixes.empty() ? 0 : prefixes.top().second);
	t_iter->second.add_line();
	t_iter->second.add_line();
	add_line = &mx_context::add_line_markup;
}

void mx_context::itemize(const string &line)
{
	envs.push(make_pair("itemize", &mx_context::end_itemize));
	unsigned int indent(prefixes.empty() ? 0 : prefixes.top().second
						 + 3);

	t_iter = doc_iter;
	t_iter->second.add_line();
	prefixes.push(make_pair("* ", indent));
}

void mx_context::enumerate(const string &line)
{
	envs.push(make_pair("enumerate", &mx_context::end_itemize));
	unsigned int indent(prefixes.empty() ? 0 : prefixes.top().second
						    + 3);

	t_iter = doc_iter;
	t_iter->second.add_line();
	prefixes.push(make_pair("#. ", indent));
}

void mx_context::end_itemize(const string &line)
{
	t_iter = doc_iter;
	t_iter->second.add_line();
	prefixes.pop();

	if (prefixes.empty())
		t_iter->second.set_indent(0);
	else
		t_iter->second.set_indent(prefixes.top().second);
}

/* Handled as a definition list */
void mx_context::table(const string &line)
{
	envs.push(make_pair("table", &mx_context::end_table));
	unsigned int indent(prefixes.empty() ? 0 : prefixes.top().second
						    + 3);

	t_iter = doc_iter;
	t_iter->second.add_line();
	prefixes.push(make_pair(string(), indent));
}

void mx_context::end_table(const string &line)
{
	t_iter = doc_iter;
	t_iter->second.add_line();
	prefixes.pop();

	if (prefixes.empty())
		t_iter->second.set_indent(0);
	else
		t_iter->second.set_indent(prefixes.top().second);
}

void mx_context::multitable(const string &line)
{
	envs.push(make_pair("multitable", &mx_context::end_multitable));
	unsigned int indent(prefixes.empty() ? 0 : prefixes.top().second
						 + 3);

	t_iter = doc_iter;
	t_iter->second.add_line();
	prefixes.push(make_pair(string(), indent));
}

void mx_context::end_multitable(const string &line)
{
	t_iter = doc_iter;
	t_iter->second.add_line();
	prefixes.pop();

	if (prefixes.empty())
		t_iter->second.set_indent(0);
	else
		t_iter->second.set_indent(prefixes.top().second);
}

void mx_context::end(const string &line)
{
	if (!line.empty()) {
		if (envs.empty()
		    || (line != envs.top().first))
			throw runtime_error((boost::format("unbalanced end tag "
							   "%1% at %2%")
					     % line % in.back().location())
					    .str());

		envs.top().second(this, line);
		envs.pop();
	}
}

void mx_context::item(const string &line)
{
	decltype(mx_item_tags.begin()) iter;

	if (envs.empty()
	    || (mx_item_tags.end()
		== (iter = mx_item_tags.find(envs.top().first)))) {
		cerr << "ignoring loose @item at " << in.back().location()
		     << endl;
		return;
	}

	iter->second(this, line);
}

void mx_context::tab(const string &line)
{
	decltype(mx_item_tags.begin()) iter;

	if (envs.empty()
	    || (mx_item_tags.end()
		== (iter = mx_item_tags.find(envs.top().first)))) {
		cerr << "ignoring loose @tab at " << in.back().location()
		     << endl;
		return;
	}

	add_line_markup(line);
	t_iter->second.set_indent(prefixes.top().second + 6);
}


void mx_context::item_itemize(const string &line)
{
	t_iter = doc_iter;
	t_iter->second.add_line();
	t_iter->second.set_indent(prefixes.top().second);
	add_line_markup(prefixes.top().first + line);
	t_iter->second.set_indent(prefixes.top().second
				  + prefixes.top().first.size());
}

void mx_context::item_table(const string &line)
{
	t_iter = doc_iter;
	t_iter->second.add_line();
	t_iter->second.set_indent(prefixes.top().second);
	add_line_markup(prefixes.top().first + line);
	t_iter->second.set_indent(prefixes.top().second + 3);
}

void mx_context::subblock(const string &line)
{
	envs.push(make_pair("{", &mx_context::end_subblock));
}

void mx_context::end_subblock(const string &line)
{
	if (envs.empty() || ("{" != envs.top().first))
		cerr << (boost::format("unbalanced subblock end at "
				       "%1% - ignoring.")
			 % in.back().location()) << endl;
	else {
		t_iter->second.textref_formatter(
				bind(&mx_context::add_line_asis,
				     this, placeholders::_1),
				in.back().name.filename(),
				in.back().line_cnt + 1
		);
		envs.pop();
	}
}

void mx_context::subblock1(const string &line)
{
	envs.push(make_pair("(", &mx_context::end_subblock));
	add_line_prev = add_line;
	add_line = &mx_context::add_line_noop;
}

void mx_context::end_subblock1(const string &line)
{
	if (envs.empty() || ("(" != envs.top().first))
		throw runtime_error((boost::format("unbalanced subblock type 1 "
						  "end at %1%.")
				     % in.back().location()).str());
	else {
		envs.pop();
		add_line = add_line_prev;
	}
}

void mx_context::qtex(const string &line)
{
	t_iter = doc_iter;
/*
	t_iter->second.add_line("raw:: latex");
	t_iter->second.add_line();
	auto_end = &mx_context::end_qtex;
*/
}

void mx_context::end_qtex(const string &tag)
{
/*
	t_iter = doc_iter;
	t_iter->second.add_line();
	t_iter->second.add_line();
	auto_end.reset();
*/
}

void mx_context::tex(const string &line)
{
	/* Just ignore qtex tags as mostly misplaced */

	envs.push(make_pair("tex", &mx_context::end_tex));

	t_iter = doc_iter;
}

void mx_context::iftex(const string &line)
{
	/* Just ignore qtex tags as mostly misplaced */

	envs.push(make_pair("iftex", &mx_context::end_tex));

	t_iter = doc_iter;
}

void mx_context::end_tex(const string &line)
{
	t_iter = doc_iter;
}

void mx_context::ifset(const string &line)
{
	envs.push(make_pair("ifset", &mx_context::end_if));
	t_iter = doc_iter;

	auto iter(defines.find(line));
	if (iter != defines.end()) {
		parse_line = &mx_context::parse_line_doc;
		add_line = &mx_context::add_line_markup;
	} else {
		parse_line = &mx_context::parse_line_literal;
		add_line = &mx_context::add_line_noop;
	}
}

void mx_context::ifclear(const string &line)
{
	envs.push(make_pair("ifclear", &mx_context::end_if));
	t_iter = doc_iter;

	auto iter(defines.find(line));
	if (iter == defines.end()) {
		parse_line = &mx_context::parse_line_doc;
		add_line = &mx_context::add_line_markup;
	} else {
		parse_line = &mx_context::parse_line_literal;
		add_line = &mx_context::add_line_noop;
	}
}

void mx_context::end_if(const string &line)
{
	t_iter = doc_iter;
	add_line = &mx_context::add_line_markup;
}

void mx_context::generic_tag(const string &tag, const string &line)
{
	// cerr << "gen tag: " << tag << " line: " << line << endl;

	t_iter = out_files.find(tag);

	if (t_iter == out_files.end()) {
		t_iter = out_files.insert(make_pair(tag, target(tag)))
				  .first;

		if (!info_lines.empty())
			t_iter->second.comment_formatter(
				bind(&mx_context::add_line_asis,
				     this, placeholders::_1), info_lines
			);
	}

	end_pos = t_iter->second.line_cnt;
	auto_end = &mx_context::end_generic_tag;
	add_line = &mx_context::add_line_text;

	t_iter->second.textref_formatter(
		bind(&mx_context::add_line_asis,
		     this, placeholders::_1),
		in.back().name.filename(),
		line.empty() ? in.back().line_cnt + 1
			     : in.back().line_cnt
	);

	if (!line.empty())
		add_line(this, line);
}

static string litinc_editor(size_t s, size_t e)
{
	return (boost::format("   :lines: %1%-%2%") % s % e).str();
}

static string litinc_head_editor(string f_name, size_t s, size_t e)
{
	return (boost::format("%1%: %2% - %3%") % f_name % s % e).str();
}

static string litinc_secline_editor(string f_name, char header, size_t s,
				    size_t e)
{
	size_t len((boost::format("%1%: %2% - %3%") % f_name % s % e)
		   .str().size());
	return string(len, header);
}

void mx_context::end_generic_tag(const string &tag)
{
	using namespace placeholders;

	auto x_iter(t_iter);
	auto c_pos(x_iter->second.line_cnt);

	t_iter = doc_iter;

	if (c_pos - end_pos) {
		end_pos++;

		string f_name((boost::format("%1%.%2%")
			       % in.back().base_name % (*x_iter).first).str());

		t_iter->second.add_line();
		t_iter->second.add_line_ref((*x_iter).first,
					    bind(litinc_head_editor,
						 f_name, _1, _2),
					    end_pos, c_pos);
		t_iter->second.add_line_ref((*x_iter).first,
					    bind(litinc_secline_editor, f_name,
						 get_level_head(SUBSECT_LVL),
						 _1, _2),
					    end_pos, c_pos);
		t_iter->second.add_line(".. literalinclude:: " + f_name);
		t_iter->second.add_line("   :language: guess");
		t_iter->second.add_line_ref((*x_iter).first, litinc_editor,
					    end_pos, c_pos);
		t_iter->second.add_line();
	}

	auto_end.reset();
	parse_line = &mx_context::parse_line_doc;
	add_line = &mx_context::add_line_markup;
}

string mx_context::replace_doc_tags(const bx::smatch &what)
{
	string sel((*what.nested_results().begin())[1]);
	string val((*what.nested_results().begin())[2]);

	if (sel.empty())
		return what[0];

	string tail((*what.nested_results().begin())[3]);
	string num((*what.nested_results().begin())[4]);
	string x_tail(tail.empty() ? string("\\ ") : tail);
	vector<string> out;

	if (!num.empty())
		x_tail = "\\ " + num + tail;

	switch (sel[0]) {
	case 'v': // @verb, @sc, @code
	case 'c':
	case 'o':
		return (boost::format("``%1%``%2%")
			% trim(make_pair(val.begin() + 1, val.end() - 1))
			% x_tail).str();
	case '`': { // code
		int i_num(0);

		if (!num.empty())
			x_tail = tail.empty() ? string("\\ ") : tail;

		cerr << "index " << num << " entry at "
		     << in.back().location() << " - ignored." << endl;

		return (boost::format("``%1%``%2%")
			% trim(make_pair(val.begin(), val.end()))
			% x_tail).str();
	}
	case '%': // emph
		return (boost::format("*%1%*%2%")
			% trim(make_pair(val.begin(), val.end()))
			% x_tail).str();
	case 'e': // @emph
		return (boost::format("*%1%*%2%")
			% trim(make_pair(val.begin() + 1, val.end() - 1))
			% x_tail).str();
	case '#': // strong
		return (boost::format("**%1%**%2%")
			% trim(make_pair(val.begin(), val.end()))
			% x_tail).str();
	case 's': // @strong
		return (boost::format("**%1%**%2%")
			% trim(make_pair(val.begin() + 1, val.end() - 1))
			% x_tail).str();
	case 't': // @cite
		return (boost::format("[%1%]_%2%")
			% trim(make_pair(val.begin() + 1, val.end() - 1))
			% tail).str();
	case '[': // link
		parse_href(out, make_pair(val.begin(), val.end()));
		if (out.empty())
			return num + tail;
		// deliberate fall-through
	case 'u': // @url
		if (out.empty())
			split_csv(out, make_pair(val.begin() + 1,
						 val.end() - 1));

		switch (out.size()) {
		case 0:
			return tail;
		case 1:
			return (boost::format("`<%1%>`_%2%") % out[0] % x_tail)
				.str();
		default:
			return (boost::format("`%1%<%2%>`_%3%")
				% out[1] % out[0] % x_tail).str();
		}
	case 'i': // @image
		split_csv(out, make_pair(val.begin() + 1, val.end() - 1));

		if (out.empty())
			return tail;

		extra_lines.push_back((boost::format(".. |image_%1%| image:: "
						     "%2%.*")
				       % image_cnt % out[0]).str());
		return ((boost::format("|image_%1%|%2%")
			 % (image_cnt++) % x_tail).str());
	};

	return what[0];
}

string mx_context::replace_text_tags(const bx::smatch &what)
{
	string sel(what[1]);

	if (sel.empty())
		return what[0];

	if (sel[0] == '`') {
		cerr << (boost::format("text index reference %1% at %2%) - "
				       "ignored") % what[3]
						  % in.back().location())
		     << endl;
		return what[2];
	}

	return what[0];
}

static string replace_macro_args(const vector<string> &args,
				 const bx::smatch &what)
{
	auto p(boost::lexical_cast<unsigned int>(what[2]));

	if (!what[3].length()) {
		if (p < 1)
			return what.str(0);
		else if (p > args.size())
			return string();
		else
			return args[p - 1];
	} else {
		if ((p < 1) || (p > args.size()) || args[p - 1].empty())
			return "@!!";
		else
			return "@";
	}
}

static void push_line(mx_context::line_block_t &out, unsigned int indent,
		      const string &in)
{
	out.push_back(make_pair(in, 0));
	out.push_back(make_pair(pad_string(indent), 0));
}

void mx_context::expand_macros(line_block_t &out, const macro_line_t &in,
			       const target &t, unsigned int indent,
			       unsigned int pass)
{

	macro_line_t m_line;
	string::const_iterator b_iter, e_iter;
	unsigned int line_pos;

	if (!saved_line.first.empty()) {
		m_line.first = saved_line.first + in.first;
		saved_line.first.clear();
		b_iter = m_line.first.begin();
		e_iter = m_line.first.end();
		line_pos = m_line.second;
	} else {
		b_iter = in.first.begin();
		e_iter = in.first.end();
		line_pos = in.second;
	}

	bx::smatch what;
	bx::sregex_iterator no_match;

	if (out.empty())
		out.push_back(make_pair(pad_string(indent), 0));

	while (bx::regex_search(b_iter, e_iter, what, macro_ref_expr)) {
		if (what.str(3) == "\\") {
			saved_line = make_pair(what.str(1), line_pos);
			return;
		}

		auto m_iter(macros.find(what.str(1)));
		auto t_indent(find_print_length(what.prefix()));
		out.back().first += what.prefix();

		if (m_iter == macros.end()) {
			if (pass > 1)
				cerr << "pass " << pass << ": undefined macro "
				     << what[1] << ", ignoring for now" << endl;

			out.back().first += what[0];
			out.back().second = line_pos;
		} else {
			t.textref_formatter(
				bind(push_line, ref(out), indent,
				     placeholders::_1),
				m_iter->second.f_name, m_iter->second.b_pos
			);

			vector<string> m_vars;

			if (what[2].length())
				split_csv(
					m_vars,
					make_pair(what[2].first + 1,
						  what[2].second - 1),
					csv_paren_expr
				);
#ifdef DEBUG_MACROS
			cerr << "t |" << what[0] << "|" << endl;
			cerr << "  m |" << what[1] << "|" << endl;
			int x_cnt(0);
			BOOST_FOREACH(const string &s, m_vars)
				cerr << "   v " << x_cnt++ << " |" << s << "|"
				     << endl;
#endif
			function< string (const bx::smatch&) > args_f(
				bind(replace_macro_args, cref(m_vars),
				     placeholders::_1));

			BOOST_FOREACH(auto const &s, m_iter->second.lines) {
				string m_out(bx::regex_replace(s.first,
							       macro_arg_expr,
							       args_f));
#ifdef DEBUG_MACROS
				cerr << "m_out >>>" << m_out << endl;
#endif
				expand_macros(out, make_pair(m_out, s.second),
					      t, indent + t_indent, pass);
			}

			t.textref_formatter(
				bind(push_line, ref(out), indent,
				     placeholders::_1),
				m_iter->second.f_name, line_pos
			);
		}
		b_iter = what.suffix().first;
	}
	out.back().first.append(b_iter, e_iter);

	out.back().first.assign(
		bx::regex_replace(out.back().first, dead_macro_expr, string())
	);
	out.push_back(make_pair(pad_string(indent), 0));
}

void mx_context::late_expand::operator()(target::line_mark &m)
{
	m.begin += delta;
	m.end += delta;

	line_block_t lines;
	context.expand_macros(lines, make_pair(string(m.begin, m.end - 1),
					       m.src_pos), *(m.t), 0, 2);

	while (!lines.empty() && lines.back().first.empty())
		lines.pop_back();

	if (!lines.empty()) {
		__gnu_cxx::crope t_out;

		BOOST_FOREACH(auto const &s, lines) {
			t_out += m.padding.c_str();
			t_out += s.first.c_str();
			t_out += "\n";
		}

		size_t x_delta(t_out.size() - (m.end - m.begin));
		m.t->body.replace(m.begin, m.end, t_out);
		m.end += x_delta;
		delta += x_delta;
		m.t->line_exp[m.line_num] = lines.size();
	} else
		m.t->line_exp[m.line_num] = 0;
}

void mx_context::ref_edit::operator()(target::line_ref &m)
{
	auto r_tgt(context.out_files.find(m.tag));

	if (r_tgt == context.out_files.end())
		return;

	auto p(r_tgt->second.line_exp.begin());
	auto q(r_tgt->second.line_exp.upper_bound(m.lines.first));
	auto r(r_tgt->second.line_exp.upper_bound(m.lines.second));

	int delta(0);
	for (; p != q; ++p) {
		if (!p->second) {
			if (delta)
				--delta;
		} else if (p->second > 1)
			delta += p->second - 1;
	}
	size_t start(m.lines.first + delta);
	int x_delta(delta);

	for (; p != r; ++p) {
		if (!p->second) {
			if (delta)
				--delta;
		} else if (p->second > 1)
			delta += p->second - 1;
	}
	size_t end(m.lines.second + delta);

	if (delta || x_delta) {
		string t_str(m.editor(start, end) + "\n");
		ssize_t tb_delta(t_str.size() - (m.end - m.begin));

		m.t->body.replace(m.begin + b_delta, m.end + b_delta,
				  t_str.c_str());
		b_delta += tb_delta;
	}
}

void mx_context::add_line_markup(const string &line)
{
	string t_str(
		bx::regex_replace(line, doc_tag_expr,
				  function<string (const bx::smatch&)>(
					bind(&mx_context::replace_doc_tags,
					     this, placeholders::_1)
				  )
		)
	);

	add_line_expand(t_str);

	while(!extra_lines.empty()) {
		t_iter->second.add_line(extra_lines.back());
		extra_lines.pop_back();
	}

}

void mx_context::add_line_text(const string &line)
{
	string t_str(
		bx::regex_replace(line, text_mark_expr,
				  function<string (const bx::smatch&)>(
					bind(&mx_context::replace_text_tags,
					     this, placeholders::_1)
				  )
		)
	);

	add_line_expand(t_str);

}

void mx_context::add_line_expand(const string &line)
{
	line_block_t lines;
	expand_macros(lines, make_pair(line, in.back().line_cnt),
		      t_iter->second);

	while (!lines.empty() && lines.back().first.empty())
		lines.pop_back();

	if (!lines.empty()) {
		/* some macros can be defined after possible expansion */
		BOOST_FOREACH(auto const &s, lines) {
			if (s.second)
				t_iter->second.add_line_mark(s.first, s.second);
			else
				t_iter->second.add_line(s.first);
		}
	} else
		t_iter->second.add_line();
}

void mx_context::add_line_macro(const string &line)
{
	if (c_macro != macros.end())
		c_macro->second.lines.push_back(
			make_pair(line, in.back().line_cnt));
}

void mx_context::add_line_menu(const string &line)
{
	static const bx::sregex menu_line_expr(
		bx::bos >> *bx::_s >> '*' >> *bx::_s >> (bx::s1 = -+bx::_)
			>> *bx::_s >> "::" >> *bx::_s >> (bx::s2 = -*bx::_)
			>> *bx::_s >> bx::eos);
	bx::smatch what;

	if (bx::regex_match(line, what, menu_line_expr)) {
		t_iter->second.add_line((boost::format("* _`%1%`: %2%")
					 % what[1] % what[2]).str());
	}
}

void mx_context::add_line_info(const string &line)
{
	info_lines.push_back(line);
}

void mx_context::add_line_asis(const string &line)
{
	t_iter->second.add_line(line);
}

void mx_context::add_line_noop(const string &line)
{
}

static bf::path find_relative(const bf::path &target, const bf::path &ref)
{
	bf::path rv;
	auto t_iter(target.begin()), ref_iter(ref.begin());

	do {
		if ((*t_iter) != (*ref_iter))
			break;
		else {
			++t_iter;
			++ref_iter;
		}
	} while ((t_iter != target.end()) && (ref_iter != ref.end()));

	for (; ref_iter != ref.end(); ++ref_iter)
		rv /= "..";

	for (; t_iter != target.end(); ++t_iter)
		rv /= (*t_iter);

	return rv;
}

void mx_context::parse_line_literal(const string &line)
{
	bx::smatch what;

	if (bx::regex_match(line, what, gen_mark_expr)) {
		if (!what[1].length() && (what.str(2) == "end")) {
			if (envs.empty())
				throw runtime_error((
					boost::format("literal line in "
						      "non-literal "
						      "environment at %1%")
					% in.back().location()).str());

			if (what.str(3) == envs.top().first) {
				envs.top().second(this, line);
				envs.pop();
				parse_line = &mx_context::parse_line_doc;
				return;
			}
		}
	}
	add_line(this, line);
}

void mx_context::parse_line_include(const string &line)
{
	static const bx::sregex inc_expr(bx::as_xpr('f') | '=' | "include");

	enum tag_level_t t_class(GEN_MARK_TAG);
	bx::smatch what;
	what.let(tag_class = t_class);

	if (bx::regex_match(line, what, gen_mark_expr)) {
		string tag(what[2]);
		if (regex_match(tag, inc_expr)) {
			if (auto_end)
				(*auto_end)(this, tag);

			auto iter(mx_tags.find(tag));

			if (iter != mx_tags.end())
				(*iter).second(this, what.str(3));

			return;
		} else {
			if (!regex_search(line, what, tag_class_expr)) {
				if (auto_end)
					(*auto_end)(this, tag);

				return;
			} else if (t_class == SUB_BLK_TAG)
				return;
		}
	}
	add_line(this, line);
}

void mx_context::parse_line_doc(const string &line)
{
	enum tag_level_t t_class(GEN_MARK_TAG);
	bx::smatch what;
	what.let(tag_class = t_class);

	if (bx::regex_match(line, what, gen_mark_expr)) {
		string tag(what[2]);
		string arg(what[3]);

		bx::regex_search(line, what, tag_class_expr);

		if ((t_class == GEN_MARK_TAG) && auto_end)
			(*auto_end)(this, tag);

		if (tag.empty())
			return;

		if ((t_class == GEN_MARK_TAG) || (t_class == SUB_BLK_TAG)) {
			auto iter(mx_tags.find(tag));

			if (iter != mx_tags.end())
				(*iter).second(this, arg);
			else
				generic_tag(tag, arg);

			return;
		}
	}
	add_line(this, line);
}

mx_context::mx_context(const vector<string> &includes_,
		       const string &doc_tag,
		       const set<string> &defines_)
	   :end_pos(0),
	    doc_iter(out_files.insert(make_pair(doc_tag,
						target(doc_tag))).first),
	    t_iter(doc_iter),
	    c_macro(macros.end()),
	    includes(includes_.begin(), includes_.end()),
	    defines(defines_),
	    parse_line(&mx_context::parse_line_doc),
	    add_line(&mx_context::add_line_markup),
	    min_sec_lvl(-1),
	    abs_sec_lvl(TITLE_LVL),
	    rel_sec_lvl(TITLE_LVL),
	    image_cnt(0),
	    modulename_set(false)
{
	string t_str;

	/* "includes" is supposed to contain directory pathes, first member is
	 * an exception, containing the actual input file path.
	 */
	in.push_back(new in_file(bf::system_complete(includes_[0])));
	if (!in.front().s.is_open())
		throw runtime_error((boost::format("couldn't access file %1%")
				     % in.front().name).str());

	/* Now, it's pathes all the way down. */
	includes[0].remove_filename();

	while(true) {
		while (!in.back().s.eof() && std::getline(in.back().s, t_str)) {
			in.back().line_cnt++;
			parse_line(this, t_str);
		}

		if (auto_end)
			(*auto_end)(this, string());

		if (in.size() == 2) {
			t_iter = doc_iter;

			bf::path rel_path(find_relative(in[1].name.parent_path()
							     .file_string(),
							in[0].name.parent_path()
							     .file_string()));

			rel_path /= in[1].base_name;

			t_iter->second.add_line();
			t_iter->second.add_line((boost::format("Include "
							       ":doc:`%1%`.")
						 % rel_path.file_string())
						.str());
		}

		if (in.size() > 1)
			in.pop_back();
		else
			break;

		if (in.size() == 1)
			parse_line = &mx_context::parse_line_doc;
	}

	for (auto t = out_files.begin(); t != out_files.end(); ++t) {
		late_expand m_expand(*this);

		for_each(t->second.line_marks.begin(),
			 t->second.line_marks.end(),
			 boost::apply_visitor(m_expand));
	}

	for (auto t = out_files.begin(); t != out_files.end(); ++t) {
		ref_edit m_edit(*this);

		for_each(t->second.line_marks.begin(),
			 t->second.line_marks.end(),
			 boost::apply_visitor(m_edit));
	}

}

void mx_context::write_out(const bf::path &prefix)
{
	ofstream ofile;
	bf::path f_path(prefix / in.front().base_name);

	for (auto t = out_files.begin(); t != out_files.end(); ++t) {
		auto x_path(f_path); /*There can be some funny extensions */
		x_path.replace_extension(t->first);

		ofile.open(x_path.file_string().c_str(), ios::binary);
		if (ofile.is_open()) {
			ofile << t->second.body;
			ofile.close();
		}
	}
}

struct toc_entry_t {
	bf::path src_path;
	string name;
	string desc;

	toc_entry_t(const string &f_name) : src_path(f_name) {}
	bf::path parent_path() const { return src_path.parent_path(); }
	operator const char *() { return src_path.file_string().c_str(); }
};

void write_index(const bf::path &index_path, const vector<toc_entry_t> &toc)
{
	ofstream ofile;
	bf::path full_index_path(bf::system_complete(index_path).parent_path());

	ofile.open(index_path.file_string().c_str(), ios::binary);
	if (!ofile.is_open())
		return;

	ofile << ".. toctree::\n   :maxdepth: 2\n\n";

	BOOST_FOREACH(const toc_entry_t &t, toc) {
		bf::path rel_path(find_relative(bf::system_complete(t.src_path)
							.parent_path(),
						full_index_path));
		rel_path /= t.name;


		if (!t.desc.empty())
			ofile << boost::format("   %1% <%2%>\n")
				 % t.desc % rel_path.file_string();
		else
			ofile << "   " << rel_path << endl;
	}

	ofile.close();
}

int main(int argc, char **argv)
{
	namespace po = boost::program_options;

	string doc_tag, index;
	vector<string> sources;
	vector<toc_entry_t> toc;
	vector<string> includes;
	vector<string> defines;
	int rc(0);

	po::options_description desc("Options:");
	desc.add_options()
		("help,h", "produce this help message")
		("doc,d", po::value<string>(&doc_tag)
			     ->default_value("rst"),
		 "file suffix to put documentation into")
		("index,x", po::value<string>(&index),
		 "generate and write out index file")
		("include,I", po::value< vector<string> >(&includes)
			      ->composing(),
		 "search additional paths for MX includes")
		("define,D", po::value< vector<string> >(&defines)
			      ->composing(),
		 "define MX processing flags");

	po::options_description src_desc("source files");
	src_desc.add(desc)
		.add_options()
		 ("sources", po::value< vector<string> >(&sources)->composing(),
		  "sources");

	po::positional_options_description src_pos;
	src_pos.add("sources", -1);

	po::variables_map desc_map;
	po::store(po::command_line_parser(argc, argv)
		  .options(src_desc).positional(src_pos).run(), desc_map);
	po::notify(desc_map);

	if (desc_map.count("help")) {
		cout << "mx2sphinx version 1.0" << endl;
		cout << "Usage: mx2sphinx [OPTION]... [FILE]..." << endl;
		cout << desc;
		return rc;
	}

	includes.insert(includes.begin(), string());

	if (!desc_map.count("sources"))
		return 0;

	ifstream ifile;

	BOOST_FOREACH(string f, sources) {
		toc.push_back(f);
		includes[0] = f;

		try {
			mx_context mx(includes, doc_tag,
				      set<string>(defines.begin(),
						  defines.end()));
			mx.write_out(toc.back().parent_path());
			toc.back().name = mx.in.front().base_name;
			toc.back().desc = mx.ref_name;
		} catch (runtime_error err) {
			cerr << "runtime error: " << err.what()
			     << endl;
			toc.pop_back();
			rc = -1;
		}
	}

	if (!index.empty())
		write_index(bf::path(index), toc);

	return rc;
}
