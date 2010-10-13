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

#include <memory>
#include <string>
#include <fstream>
#include <iostream>
#include <ext/rope>
#include <functional>
#include <initializer_list>

#include <boost/any.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/variant.hpp>
#include <boost/optional.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/compressed_pair.hpp>
#include <boost/xpressive/regex_actions.hpp>
#include <boost/xpressive/xpressive_static.hpp>

//#define DEBUG_MACROS

namespace mx_gr {
using namespace boost::xpressive;

typedef __gnu_cxx::crope storage_t;
typedef __gnu_cxx::crope::iterator iter_t;
typedef __gnu_cxx::crope::const_iterator const_iter_t;
typedef basic_regex<const_iter_t> rregex;
typedef match_results<const_iter_t> rmatch;
typedef sub_match<const_iter_t> rsub_match;
typedef regex_iterator<const_iter_t> rregex_iterator;

#define ACTION_SUFFIX _n

#define ACTION_LIST (literal1_tag) \
		    (literal2_tag) \
		    (index_tag)    \
		    (control_tag)  \
		    (macro_tag)    \
/**/

#define ACTION_TAG_NAME(r, data, elem) \
	BOOST_PP_COMMA_IF(r)           \
	BOOST_PP_CAT(elem, data)       \
/**/

enum action_name {
	BOOST_PP_CAT(null_tag, ACTION_SUFFIX)
	BOOST_PP_SEQ_FOR_EACH(ACTION_TAG_NAME, ACTION_SUFFIX, ACTION_LIST)
};

#define ACTION_METHOD(r, data, elem)                                  \
	virtual void elem(rsub_match const &s1, rsub_match const &s2, \
			  rsub_match const &s3) const {}              \
/**/

struct actions {
	virtual ~actions() {}
	virtual std::shared_ptr<actions> instance(boost::any arg) {}

	BOOST_PP_SEQ_FOR_EACH(ACTION_METHOD, , ACTION_LIST)
};

const placeholder<actions> _actions{};

template<action_name T>
struct action_impl {
	typedef void result_type;

	void operator()(actions const &act, rsub_match const &s1,
			rsub_match const &s2, rsub_match const &s3) const;
};

#define ACTION_IMPL(r, data, elem)                                     \
	template<>                                                     \
	void action_impl<BOOST_PP_CAT(elem, data)>                     \
	::operator()(actions const &act, rsub_match const &s1,         \
		     rsub_match const &s2, rsub_match const &s3) const \
	{                                                              \
		act.elem(s1, s2, s3);                                  \
	}                                                              \
                                                                       \
	function< action_impl<BOOST_PP_CAT(elem, data)> >::type const  \
	elem{};                                                        \
/**/


BOOST_PP_SEQ_FOR_EACH(ACTION_IMPL, ACTION_SUFFIX, ACTION_LIST)

const rregex null_expr;

const rregex line(
	(s1 = -*_) >> *blank >> _ln
);

const rregex inline_sep(
	!(as_xpr('\\') >> *blank) >> _ln >> *blank
);

const rregex braces(
	(~after('\\') >> '{')
	>> *(by_ref(braces) | keep(*(~(set = '{','}')
				   | (after('\\') >> (set = '{','}')))))
	>> (~after('\\') >> '}')
);

const rregex parens(
	(~after('\\') >> '(')
	>> *(by_ref(parens) | keep(*(~(set = '(',')')
				   | (after('\\') >> (set = '(',')')))))
	>> (~after('\\') >> ')')
);

const rregex csv_char(
	 (after('\\') >> ',') | ~as_xpr(',')
);

const rregex simple_csv(
	(bos | (~after('\\') >> ','))
	>> (s1 = *csv_char)
);

const rregex paren_csv(
	(bos | (~after('\\') >> ','))
	>> (s1 = *(keep(parens) | (after('\\') >> ',') | ~as_xpr(',')))
);

const rregex href(
	"<a" >> +_s >> -*_ >> "href" >> *_s >> '=' >> *_s
	     >> ((~after('\\') >> '"' >> (s1 = -*_) >> ~after('\\') >> '"')
		 | (s1 = *(~_s | (after('\\') >> _s))))
	     >> -*_ >> '>' >> *_s >> (s2 = -*_) >> *_s >> "</a>"
);

const rregex at(
	~after('\\') >> '@'
);

// Inline markup tags, type 1

const rregex link(
	(at >> '[' >> *blank >> (s1 = ("<a" >> -*_ >> "</a>")) >> *blank >> at
	 >> (s2 = !(_ln | blank)))
	[_actions->*literal1_tag(_, s1, s2)]
);

const rregex literal1(
	(at >> (set = '%', '#') >> *blank >> (s1 = -*_) >> *blank >> at
	    >> (s2 = !(_ln | blank)))
	[_actions->*literal1_tag(_, s1, s2)]
);

const rregex comment(
	(at >> '\'' >> (s1 = -*_) >> _ln)
	[_actions->*literal1_tag(_, s1, s2)]
);


// Inline markup tags, type 2

const rregex literal2(
	(at >> -+~_s >> (s1 = braces) >> (s2 = !(_ln | blank)))
	[_actions->*literal2_tag(_, s1, s2)]
);

// Word index tags

const rregex index_ref(
	(at >> '`' >> (s1 = -*_) >> at >> _d >> (s2 = !(_ln | blank)))
	[_actions->*index_tag(_, s1, s2)]
);

// Macro tags

const rregex macro_arg(
	at >> ((s1 = _d) | ('[' >> (s1 = +_d) >> ']'))
);

const rregex cond_macro_ref(
	(at >> '?' >> macro_arg >> ':' >> (s1 = -+~_s)
	    >> !(s2 = keep(parens)) >> (at | (*blank >> _ln)))
	[_actions->*macro_tag(_, s1, s2)]
);

const rregex macro_ref(
	(at >> ':' >> (s1 = -+~_s) >> !(s2 = keep(parens))
		   >> (at | (*blank >> _ln)))
	[_actions->*macro_tag(_, s1, s2)]
);

// Control tags

const rregex shipout_tag(
	((bos | _ln) >> (s1 = ('@' >> (s2 = (set = '{', '}', '(', ')')))))
	[_actions->*control_tag(s1, s2, s3)]
);

const rregex stop_tag(
	((bos | _ln) >> (s1 = ('@' >> (blank | (s3 = _ln) | eos))))
	[_actions->*control_tag(s1, s2, s3)]
);

const rregex tag(
	((bos | _ln) >> (s1 = ('@' >> (s2 = +~_s) >> *blank)))
	[_actions->*control_tag(s1, s2, s3)]
);

const rregex verb_line(
	(s1 = ((s3 = _ln) | eos))
	[_actions->*control_tag(s1, s2, s3)]
);

const rregex macro_tags(
	macro_ref | cond_macro_ref | macro_arg
);

const rregex inline_text_tags(
	index_ref | macro_tags
);

const rregex inline_doc_tags(
	link | literal1 | literal2 | index_ref | macro_tags
);

const rregex flow_tags(
	stop_tag | tag
);

const rregex text_tags(
	inline_text_tags | shipout_tag | flow_tags
);

const rregex doc_tags(
	inline_doc_tags | shipout_tag | flow_tags
);

const rregex doc_line(
	inline_doc_tags | verb_line
);

}

using namespace std;
using namespace std::placeholders;
namespace bx = boost::xpressive;
namespace bf = boost::filesystem;

static const unsigned int tab_size(8);

static mx_gr::storage_t pad_string(unsigned int p_size)
{
	return mx_gr::storage_t(p_size / tab_size, '\t')
	       .append(p_size % tab_size, ' ');
}

/* Temporary replacement until library stuff gets fixed */
template <typename storage_t, typename iter_t, typename formatter_t>
static storage_t regex_replace(iter_t b_iter, iter_t e_iter,
			       bx::basic_regex<iter_t> const &expr,
			       formatter_t formatter)
{
	bx::match_results<iter_t> what;
	storage_t out;

	while (bx::regex_search(b_iter, e_iter, what, expr)) {
		out.append(what.prefix().first, what.prefix().second);
		out.append(formatter(what));
		b_iter = what.suffix().first;
	}

	if (b_iter != e_iter)
		out.append(b_iter, e_iter);

	return out;
}

typedef pair<string, size_t> loc_t;

// Line + some optional metadata.
// Compressed piar is slighly more convenient than normal pair.
struct line_t : protected boost::compressed_pair<mx_gr::storage_t, loc_t> {
	typedef boost::compressed_pair<first_type, second_type> this_type;

	line_t()
	:this_type()
	{}

	line_t(size_t pos)
	:this_type(second_type(string(), pos))
	{}

	line_t(size_t n, char c)
	:this_type(first_type(n ,c))
	{}

	line_t(char const *data)
	:this_type(first_type(data))
	{}

	line_t(string const &data)
	:this_type(first_type(data.c_str()))
	{}

	line_t(first_type const &data)
	: this_type(data)
	{}

	line_t(second_type const &loc)
	: this_type(loc)
	{}

	line_t(mx_gr::rsub_match const &data)
	: this_type(first_type(data.first, data.second))
	{}

	line_t(first_type const &data, second_type const &loc)
	: this_type(data, loc)
	{}

	line_t(mx_gr::rsub_match const &data, second_type const &loc)
	: this_type(first_type(data.first, data.second), loc)
	{}

	line_t &append(mx_gr::rsub_match const &range)
	{
		first().append(range.first, range.second);
		return *this;
	}

	line_t &append(line_t const &line)
	{
		if (line.marked()) {
			if (marked())
				BOOST_ASSERT(second() == line.second());
			else
				second() = line.second();
		}

		first().append(line.first());
	}

	void clear()
	{
		first().clear();
	}

	bool marked() const
	{
		return !second().first.empty();
	}

	bool empty() const
	{
		return first().empty();
	}

	size_t const &line_pos() const
	{
		return second().second;
	}

	size_t &line_pos()
	{
		return second().second;
	}

	first_type const &value() const
	{
		return first();
	}

	second_type const &location() const
	{
		return second();
	}

	second_type &location()
	{
		return second();
	}

	string str() const
	{
		return string(first().begin(), first().end());
	}

	/* Bug in gcc-4.4.2 prevents using this method
	char const *c_str() const
	{
		return first().c_str();
	}
	*/
};

namespace std {

ostream &operator<<(ostream &out, mx_gr::rregex const &expr)
{
	out << expr.regex_id();
}

ostream &operator<<(ostream &out, loc_t const &loc)
{
	return out << loc.first << ":" << loc.second;
}

ostream &operator<<(ostream &out, line_t const &line)
{
	if (line.location().first.empty())
		return out << line.line_pos() << ": " << line.value() << endl;
	else
		return out << line.line_pos() << "!: " << line.value() << endl;
}

}


line_t operator+(line_t const &v1, line_t const &v2)
{
	line_t rv(v1);
	rv.append(v2);
	return rv;
}

static void null_comment(function<void (line_t const &)>  out,
			 list<line_t> const &in)
{
}

static void c_comment(function<void (line_t const &)>  out,
		      list<line_t> const &in)
{
	if (in.empty())
		return;

	if ((++in.begin()) == in.end()) {
		out("/* " + in.front() + " */");
	} else {
		out("/*");
		BOOST_FOREACH(auto const &s, in) {
			if (!s.empty())
				out(" * " + s);
			else
				out(" *");
		}
		out(" */");
	}
}

static void mal_comment(function<void (line_t const &)>  out,
			list<line_t> const &in)
{
	BOOST_FOREACH(auto const &s, in) {
		if (!s.empty())
			out("# " + s);
		else
			out("#");
	}
}

static void null_textref(function<void (string const &)>  out,
			 loc_t const &loc)
{
}

static void c_textref(function<void (string const &)>  out,
		      loc_t const &loc)
{
	out((boost::format("#line %1% \"%2%\"") % loc.second % loc.first)
	     .str());
};

typedef function<void (function<void (line_t const &)>  out,
		       list<line_t> const &in)> comment_formatter_t;
typedef function<void (function<void (string const &)>,
		       loc_t const&)> textref_formatter_t;

static const map<string, comment_formatter_t> comment_formatters {
	{"c", c_comment},
	{"h", c_comment},
	{"mal", mal_comment}
};

static const map<string, textref_formatter_t> textref_formatters {
	{"c", c_textref}
};

class target {
	comment_formatter_t comment_formatter;
	textref_formatter_t textref_formatter;
	mx_gr::storage_t padding;

	void add_raw_line(mx_gr::storage_t const &line = mx_gr::storage_t())
	{
		if (!line.empty()) {
			body += padding;
			body += line;
		}
		body += '\n';
		++line_cnt;
	}

	void add_raw_line(string const &line)
	{
		if (!line.empty()) {
			body += padding;
			body += line.c_str();
		}
		body += '\n';
		++line_cnt;
	}

	void add_line_mark(mx_gr::storage_t const &line, loc_t const &src_loc)
	{
		line_marks.push_back(line_mark(this, line, src_loc));

		line_exp.insert(make_pair(src_loc.second, 1));
	}

	void add_src_ref(loc_t const &loc)
	{
		textref_formatter(
			bind(
				static_cast<
					void (target::*)(string const &)
				>(&target::add_raw_line),
				this, _1
			), loc
		);
	}
public:
	struct line_mark {
		target *t;
		mx_gr::iter_t begin;
		mx_gr::iter_t end;
		loc_t src_loc;
		string padding;
		unsigned int line_num;

		line_mark(target *t_, mx_gr::storage_t const &line,
			  loc_t const &src_loc_)
		: t(t_),
		  begin(t->body.mutable_end()),
		  src_loc(src_loc_),
		  padding(t->padding),
		  line_num(t->line_cnt) {
			t->add_raw_line(line);
			end = t->body.mutable_end();
		}
	};

	struct line_ref {
		target *t;
		mx_gr::iter_t begin;
		mx_gr::iter_t end;
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
			t->add_raw_line(editor(s, e));
			end = t->body.mutable_end();
		}
	};

	mx_gr::storage_t body;
	unsigned int line_cnt;
	vector< boost::variant<line_mark, line_ref> > line_marks;
	map<size_t, size_t> line_exp;

	target(const string &tag = string())
	: comment_formatter(null_comment),
	  textref_formatter(null_textref),
	  line_cnt(0)
	{

		auto c_iter(comment_formatters.find(tag));
		if (c_iter != comment_formatters.end())
			comment_formatter = c_iter->second;

		auto t_iter(textref_formatters.find(tag));
		if (t_iter != textref_formatters.end())
			textref_formatter = t_iter->second;
	}

	void set_indent(unsigned int indent = 0)
	{
		padding = pad_string(indent);
	}

	void add_line(line_t const &line = line_t())
	{
		if (!line.marked())
			add_raw_line(line.value());
		else {
			if (!line.empty())
				add_line_mark(line.value(), line.location());
			else
				add_src_ref(line.location());
		}
	}

	void add_line_ref(const string &tag,
			  function<string (size_t, size_t)> editor,
			  size_t s, size_t e)
	{
		line_marks.push_back(line_ref(this, tag, editor, s, e));
	}

	void add_comment(list<line_t> const &lines)
	{
		comment_formatter(
			bind(&target::add_line, this, _1), lines
		);
	}
};

struct mx_context {
	typedef function<void (mx_context*, bool)> tag_handler_t;
	typedef function<void (mx_context*, line_t const &)> line_handler_t;
	typedef list<line_t> line_list_t;

	enum sec_level_t {
		TITLE_LVL = 0,
		MODULE_LVL,
		SECT_LVL,
		SUBSECT_LVL,
		PAR_LVL
	};

	struct tag_t {
		enum {
			NONTERM = 0x1,
			INCFILE = 0x2,
			ONESHOT = 0x4
		};

		mx_gr::rregex const *expr;
		shared_ptr<mx_gr::actions> actions;
		tag_handler_t handler;
		unsigned int flags;

		tag_t(mx_gr::rregex const &expr_,
		      shared_ptr<mx_gr::actions> actions_,
		      tag_handler_t handler_, unsigned int flags_ = 0)
		: expr(&expr_),
		  actions(actions_),
		  handler(handler_),
		  flags(flags_) {}

		tag_t(tag_t const &other, mx_context *ctx)
		: expr(other.expr),
		  actions(other.actions->instance(ctx)),
		  handler(other.handler),
		  flags(other.flags)
		{}

		bool test(unsigned int flags_) const
		{
			return (flags_ & flags) == flags_;
		}
	};

	struct macro_t {
		line_list_t lines;
		loc_t loc;

		void clear()
		{
			lines.clear();
			loc = make_pair(string(), 0);
		}
	};

	class in_file {
		tag_t def_tag;
		boost::optional<tag_t> opt_tag;
		string tag_name_str;
		mx_gr::storage_t body;
	public:
		size_t line_cnt;
		bf::path name;
		string base_name;
		mx_gr::rsub_match range;

		in_file(bf::path name_, tag_t const &init_tag)
		: def_tag(init_tag),
		  line_cnt(0),
		  name(name_),
		  base_name(bf::path(name.filename()).replace_extension()
						     .file_string())
		{
			ifstream s(name.file_string().c_str(), ios::binary);
			if (s.is_open()) {
				vector<char> buf(4096);

				do {
					s.read(&buf.front(), buf.size());
					body.append(&buf.front(), s.gcount());
				} while (!s.eof());
				s.close();
			} else
				throw runtime_error("couldn't access file"
						    + name.file_string());

			range = mx_gr::rsub_match(body.begin(),
						  body.end(), true);
		}

		string const &tag_name() const
		{
			return tag_name_str;
		}

		mx_gr::rregex const &expr() const
		{
			return opt_tag ? *opt_tag->expr : *def_tag.expr;
		}

		shared_ptr<mx_gr::actions> actions() const
		{
			return opt_tag ? opt_tag->actions
				       : def_tag.actions;
		}

		void reset_tag(mx_context *ctx)
		{
			if (opt_tag) {
				opt_tag->handler(ctx, false);
				tag_name_str.clear();
				opt_tag.reset();
			}
		}

		void set_tag(mx_context *ctx, tag_t const &tag_,
			     string const &name_ = string())
		{
			if (opt_tag)
				throw runtime_error(
					"overriding unterminated tag at "
					+ boost::lexical_cast<string>(
						location()
					  )
				);

			opt_tag = tag_t(tag_, ctx);
			tag_name_str = name_;
			opt_tag->handler(ctx, true);
		}

		loc_t location(size_t line)
		{
			return make_pair(name.file_string(), line);
		}

		loc_t location()
		{
			return make_pair(name.file_string(), line_cnt);
		}
	};

	static map<string, tag_t> mx_tags;
	static map<string, line_handler_t> mx_literal_tags;
	static map<string, tag_handler_t> mx_item_tags;
	static map<string, tag_handler_t> info_formatters;
	static vector<char> sec_heads;

	stack<in_file> in;
	string ref_name;
	map<string, target> out_files;
	decltype(out_files.begin()) doc_iter, t_iter;
	stack< pair<string, line_handler_t> > envs;
	size_t end_pos;
	vector<bf::path> includes;
	set<string> defines;
	stack< pair<string, unsigned int> > prefixes;
	int min_sec_lvl, abs_sec_lvl, rel_sec_lvl;
	int image_cnt, macro_pass;
	bool modulename_set;
	bool matched;

	line_list_t info_lines;
	line_list_t macro_lines;
	line_list_t extra_lines;
	boost::optional<line_t> matched_line;
	map<string, macro_t> macros;

	void new_line(size_t l_cnt = 1);
	void append_lines(mx_gr::rsub_match line_range);

	void add_line_default(line_t const &line);
	void add_line_list(line_t const &line, line_list_t &list);

	void add_line_text(const string &line) {};
	void add_line_expand(const string &line) {};
	void add_line_menu(const string &line) {};

	char get_level_head(sec_level_t lvl);

	void null_tag(bool open) {};
	void generic_tag(bool open);
	void info_block(bool open);
	void basename(bool open);
	void macro_def(bool open);
	void include(bool open);
	void bib_info(bool open, string const &info_tag);
	void noindent(bool open);
	void header(bool open, enum sec_level_t level);
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
	void subblock(bool open);
	void end_subblock(bool open);
	void subblock1(const string &line);
	void end_subblock1(const string &line);
	void item(const string &line);
	void tab(const string &line);
	void item_itemize();
	void item_table();

	void emph(line_t const &line);
	void strong(line_t const &line);
	void verb(line_t const &line);
	void url(line_t const &line);
	void href(line_t const &line);
	void image(line_t const &line);
	void cite(line_t const &line);

	void set_literal1(mx_gr::rsub_match const &expr,
			  mx_gr::rsub_match const &body,
			  mx_gr::rsub_match const &tail);

	void set_literal2(mx_gr::rsub_match const &expr,
			  mx_gr::rsub_match const &body,
			  mx_gr::rsub_match const &tail);

	void set_index_doc(mx_gr::rsub_match const &expr,
			   mx_gr::rsub_match const &body,
			   mx_gr::rsub_match const &tail);

	void set_index_text(mx_gr::rsub_match const &expr,
			    mx_gr::rsub_match const &body,
			    mx_gr::rsub_match const &tail);

	void add_macro_line(mx_gr::rsub_match const &expr);

	void expand_macro_r(line_list_t &m_exp, decltype(macros.begin()) m_iter,
			    vector<mx_gr::storage_t> const &args,
			    size_t indent);

	void expand_macro(mx_gr::rsub_match const &expr,
			  mx_gr::rsub_match const &name,
			  mx_gr::rsub_match const &arg);

	void set_tag(mx_gr::rsub_match const &expr,
		     mx_gr::rsub_match const &tag,
		     mx_gr::rsub_match const &nline);

	/* Act only on top-level tags */
	struct basic_actions : public mx_gr::actions {
		mx_context *ctx;

		basic_actions() : ctx(NULL) {}
		basic_actions(mx_context *ctx_) : ctx(ctx_) {}

		virtual shared_ptr<mx_gr::actions> instance(boost::any arg)
		{
			return make_shared<basic_actions>(
				boost::any_cast<mx_context *>(arg)
			);
		}

		virtual void macro_tag(mx_gr::rsub_match const &expr,
				       mx_gr::rsub_match const &name,
				       mx_gr::rsub_match const &arg) const;

		virtual void control_tag(mx_gr::rsub_match const &expr,
					 mx_gr::rsub_match const &tag,
					 mx_gr::rsub_match const &nline) const;
	};

	/* Act on top-level flags, expand macros, record index references */
	struct text_actions : public basic_actions {

		text_actions() : basic_actions() {}
		text_actions(mx_context *ctx_) : basic_actions(ctx_) {}

		virtual shared_ptr<mx_gr::actions> instance(boost::any arg)
		{
			return make_shared<text_actions>(
				boost::any_cast<mx_context *>(arg)
			);
		}

		virtual void index_tag(mx_gr::rsub_match const &expr,
				       mx_gr::rsub_match const &body,
				       mx_gr::rsub_match const &tail) const;

		virtual void macro_tag(mx_gr::rsub_match const &expr,
				       mx_gr::rsub_match const &name,
				       mx_gr::rsub_match const &arg) const;

	};

	/* Act on all MX tags */
	struct doc_actions : public text_actions {

		doc_actions() : text_actions() {}
		doc_actions(mx_context *ctx_) : text_actions(ctx_) {}

		virtual shared_ptr<mx_gr::actions> instance(boost::any arg)
		{
			return make_shared<doc_actions>(
				boost::any_cast<mx_context *>(arg)
			);
		}

		virtual void literal1_tag(mx_gr::rsub_match const &expr,
					  mx_gr::rsub_match const &body,
					  mx_gr::rsub_match const &tail) const;

		virtual void literal2_tag(mx_gr::rsub_match const &expr,
					  mx_gr::rsub_match const &body,
					  mx_gr::rsub_match const &tail) const;

		virtual void index_tag(mx_gr::rsub_match const &expr,
				       mx_gr::rsub_match const &body,
				       mx_gr::rsub_match const &tail) const;
	};


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

map<string, mx_context::tag_t> mx_context::mx_tags {
	{"/", tag_t(mx_gr::flow_tags, make_shared<text_actions>(),
		    &mx_context::info_block)},
	{"f", tag_t(mx_gr::verb_line, make_shared<basic_actions>(),
		    &mx_context::basename, tag_t::INCFILE)},
	{"=", tag_t(mx_gr::flow_tags, make_shared<basic_actions>(),
		    &mx_context::macro_def, tag_t::INCFILE)},
	{"include", tag_t(mx_gr::verb_line, make_shared<basic_actions>(),
			  &mx_context::include, tag_t::INCFILE)},
	{"a", tag_t(mx_gr::doc_line, make_shared<doc_actions>(),
		    bind(&mx_context::bib_info, _1, _2, ":Author: "))},
	{"v", tag_t(mx_gr::doc_line, make_shared<doc_actions>(),
		    bind(&mx_context::bib_info, _1, _2, ":Version: "))},
	{"d", tag_t(mx_gr::doc_line, make_shared<doc_actions>(),
		    bind(&mx_context::bib_info, _1, _2, ":Date: "))},
	{"noindent", tag_t(mx_gr::doc_line, make_shared<doc_actions>(),
			   &mx_context::noindent)},
	{"t", tag_t(mx_gr::doc_line, make_shared<doc_actions>(),
		    bind(&mx_context::header, _1, _2, TITLE_LVL))},
	{"*", tag_t(mx_gr::doc_line, make_shared<doc_actions>(),
		    bind(&mx_context::header, _1, _2, MODULE_LVL))},
	{"+", tag_t(mx_gr::doc_line, make_shared<doc_actions>(),
		    bind(&mx_context::header, _1, _2, SECT_LVL))},
	{"section", tag_t(mx_gr::doc_line, make_shared<doc_actions>(),
			  bind(&mx_context::header, _1, _2, SECT_LVL))},
	{"subsection", tag_t(mx_gr::doc_line, make_shared<doc_actions>(),
			     bind(&mx_context::header, _1, _2, SUBSECT_LVL))},
	{"-", tag_t(mx_gr::doc_line, make_shared<doc_actions>(),
		    bind(&mx_context::header, _1, _2, PAR_LVL))},
/*
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
*/
	{"{", tag_t(mx_gr::verb_line, make_shared<basic_actions>(),
		    &mx_context::subblock, tag_t::ONESHOT | tag_t::NONTERM)},
	{"}", tag_t(mx_gr::verb_line, make_shared<basic_actions>(),
		    &mx_context::end_subblock,
		    tag_t::ONESHOT | tag_t::NONTERM)},
/*
	{"(", &mx_context::subblock1},
	{")", &mx_context::end_subblock1},
	{"T", &mx_context::qtex},
	{"tex", &mx_context::tex},
	{"iftex", &mx_context::iftex},
	{"ifset", &mx_context::ifset},
	{"ifclear", &mx_context::ifclear}
*/
};

map<string, mx_context::line_handler_t> mx_context::mx_literal_tags {
	{"%", &mx_context::emph},
	{"i", &mx_context::emph},
	{"emph", &mx_context::emph},
	{"#",  &mx_context::strong},
	{"strong", &mx_context::strong},
	{"sc", &mx_context::verb},
	{"verb", &mx_context::verb},
	{"code", &mx_context::verb},
	{"cite", &mx_context::cite},
	{"url", &mx_context::url},
	{"[", &mx_context::href},
	{"image", &mx_context::image}
};

map<string, mx_context::tag_handler_t> mx_context::mx_item_tags;
/*
	{"itemize", &mx_context::item_itemize},
	{"enumerate", &mx_context::item_itemize},
	{"table", &mx_context::item_table},
	{"multitable", &mx_context::item_table}
};
*/
vector<char> mx_context::sec_heads{'#', '*', '=', '-', '^', '"'};

/* A simplistic one. */
template<typename iter_t>
static size_t find_print_length(iter_t begin, iter_t end)
{
	size_t rv(0);

	for (auto b = begin; b != end; ++b) {
		if (*b == '\t')
			rv += tab_size;
		else
			++rv;
	}

	return rv;
}

static void split_csv(vector<mx_gr::storage_t> &out,
		      mx_gr::rsub_match const &in,
		      mx_gr::rregex const &expr = mx_gr::simple_csv)
{
	mx_gr::rregex_iterator b(in.first, in.second, expr), e;

	for (; b != e; ++b)
		out.push_back(mx_gr::storage_t((*b)[1].first, (*b)[1].second));
}

static bf::path find_relative(bf::path const &target, bf::path const &ref)
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

static size_t unbreak_line(mx_gr::storage_t &out, mx_gr::rsub_match const &in)
{
	mx_gr::rmatch what;
	auto b_iter(in.first);
	size_t l_cnt(0);

	while (bx::regex_search(b_iter, in.second, what, mx_gr::inline_sep)) {
		out.append(what.prefix().first, what.prefix().second);
		out.append(' ');
		b_iter = what.suffix().first;
		++l_cnt;
	}

	if (b_iter != in.second)
		out.append(b_iter, in.second);

	return l_cnt;
}

void mx_context::new_line(size_t l_cnt)
{
	if (l_cnt) {
		if (matched_line)
			envs.top().second(this, *matched_line);
		++in.top().line_cnt;
		--l_cnt;
	}

	while (l_cnt) {
		++in.top().line_cnt;
		envs.top().second(this, line_t());
		--l_cnt;
	}

	matched_line = line_t(in.top().line_cnt);
}

void mx_context::append_lines(mx_gr::rsub_match line_range)
{
	if (!line_range.length())
		return;

	if (!matched_line)
		new_line();

	mx_gr::rmatch what;

	while (bx::regex_search(line_range.first, line_range.second, what,
				mx_gr::line)) {
		matched_line->append(what[1]);
		new_line();
		line_range.first = what.suffix().first;
	}

	matched_line->append(line_range);
}

void mx_context::add_line_default(line_t const &line)
{
	cerr << "add1 " << line << endl;
	t_iter->second.add_line(line);
}

void mx_context::add_line_list(line_t const &line, line_list_t &list)
{
	cerr << "add2 " << line << endl;
	list.push_back(line);
}


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

void mx_context::generic_tag(bool open)
{
	if (open) {
		string const &tag(in.top().tag_name());
		t_iter = out_files.find(tag);

		if (t_iter == out_files.end()) {
			t_iter = out_files.insert(make_pair(tag, target(tag)))
					  .first;

			t_iter->second.add_comment(info_lines);
		}

		t_iter->second.add_line(loc_t(in.top().name.filename(),
					      in.top().line_cnt + 1));
		end_pos = t_iter->second.line_cnt;
	} else {
		size_t b_pos(end_pos + 1);
		size_t e_pos(t_iter->second.line_cnt);

		string f_name((boost::format("%1%.%2%")
			       % in.top().base_name % t_iter->first).str());

		doc_iter->second.add_line();
		doc_iter->second.add_line_ref(
			t_iter->first,
			bind(litinc_head_editor, f_name, _1, _2),
			b_pos, e_pos
		);
		doc_iter->second.add_line_ref(
			t_iter->first,
			bind(litinc_secline_editor, f_name,
			     get_level_head(SUBSECT_LVL), _1, _2),
			b_pos, e_pos
		);
		doc_iter->second.add_line(".. literalinclude:: " + f_name);
		doc_iter->second.add_line("   :language: guess");
		doc_iter->second.add_line_ref(
			t_iter->first, litinc_editor, b_pos, e_pos
		);
		doc_iter->second.add_line();
		t_iter = doc_iter;
	}
}

void mx_context::info_block(bool open)
{
	if (open) {
		envs.push(make_pair(in.top().tag_name(),
				    bind(&mx_context::add_line_list, _1, _2,
					 std::ref(info_lines))));
	} else {
		envs.pop();

		if (!info_lines.empty() && info_lines.front().empty())
			info_lines.pop_front();
	}
}

void mx_context::basename(bool open)
{
	if (open)
		return;

	if (!matched_line)
		return;

	line_t name(*matched_line);
	matched_line.reset();

	if (!name.empty()) {
		in.top().base_name = name.str();

		if ((in.size() == 1) && !modulename_set) {
			doc_iter->second.add_line(".. module:: " + name);
			doc_iter->second.add_line();
			modulename_set = true;
		}
	}
}

/*
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
*/

void mx_context::macro_def(bool open)
{
	if (open) {
		envs.push(make_pair(in.top().tag_name(),
				    bind(&mx_context::add_line_list, _1, _2,
					 std::ref(macro_lines))));
		return;
	}


	envs.pop();

	if (macro_lines.empty())
		return;

	string name(macro_lines.front().str());
	loc_t loc(in.top().location().first,
		  macro_lines.front().line_pos());
	macro_lines.pop_front();

	if (name.empty())
		return;

	auto c_macro(macros.find(name));

	if (c_macro == macros.end()) {
		c_macro = macros.insert(make_pair(name, macro_t()))
				.first;
	} else {
		cerr << "macro " << name << " redefined at "
		     << loc << endl;
		c_macro->second.clear();
	}

	c_macro->second.loc = loc;
	c_macro->second.lines.splice(c_macro->second.lines.end(),
				     macro_lines);

	string m_title("Macro: " + name);
	doc_iter->second.add_line();
	doc_iter->second.add_line(m_title);
	doc_iter->second.add_line(string(m_title.size(),
				get_level_head(SUBSECT_LVL)));
	doc_iter->second.add_line(".. code-block:: guess");
	doc_iter->second.add_line();
	doc_iter->second.set_indent(prefixes.empty()
				    ? 3 : (prefixes.top().second + 3));

	BOOST_FOREACH(auto &s, c_macro->second.lines)
		doc_iter->second.add_line(s);

	doc_iter->second.set_indent(prefixes.empty()
				    ? 0 : prefixes.top().second);
	doc_iter->second.add_line();

	//unindent_lines(c_macro->second.lines);
}

void mx_context::include(bool open)
{
	if (open)
		return;

	if (!matched_line)
		return;

	line_t line(*matched_line);
	matched_line.reset();

	if (line.empty())
		return;

	BOOST_FOREACH(bf::path &p, includes) {
		bf::path f(bf::system_complete(p / line.str()));

	try {
		if (exists(p)) {
			in.push(in_file(f, tag_t(
					mx_gr::tag,
					make_shared<basic_actions>(this),
					&mx_context::null_tag
				)));
		return;
		}
	} catch (runtime_error &err) {
	}
	}

	cerr << "couldn't open include file " << line.value() << " at "
	     << line.location() << " - skipping." << endl;
}

void mx_context::bib_info(bool open, string const &info_tag)
{
	if (open)
		return;

	if (!matched_line)
		return;

	line_t line(*matched_line);
	matched_line.reset();

	if (!line.empty())
		doc_iter->second.add_line(info_tag + line);
}

void mx_context::noindent(bool open)
{
	if (open)
		return;

	if (!matched_line)
		return;

	line_t line(*matched_line);
	matched_line.reset();

	doc_iter->second.set_indent(0);
	doc_iter->second.add_line(line);
	doc_iter->second.set_indent(prefixes.empty()
				    ? 0 : prefixes.top().second);
}

void mx_context::header(bool open, enum sec_level_t level)
{
	if (open)
		return;

	if (!matched_line)
		return;

	line_t line(*matched_line);
	matched_line.reset();

	doc_iter->second.add_line();

	if (!line.empty()) {
		if (((level == TITLE_LVL) || (level == MODULE_LVL))
		    && ref_name.empty())
			ref_name = line.str();

		doc_iter->second.add_line(line);
		doc_iter->second.add_line(line_t(line.value().size(),
						 get_level_head(level)));
	}
}

void mx_context::node(const string &line)
{
	vector<string> nodes;
	//split_csv(nodes, make_pair(line.begin(), line.end()));
	bool e_line(false);

	;

	BOOST_FOREACH(const string &s, nodes) {
		if (!s.empty()) {
			if (!e_line) {
				doc_iter->second.add_line();
				e_line = true;
			}
			doc_iter->second.add_line((boost::format(".. _%1%:")
						 % s).str());
		}
	}

	if (e_line)
		doc_iter->second.add_line();
}

void mx_context::menu(const string &line)
{
	//envs.push(make_pair("menu", &mx_context::end_menu));

	;
	doc_iter->second.add_line();
	//parse_line = &mx_context::parse_line_literal;
	//add_line = &mx_context::add_line_menu;
}

void mx_context::end_menu(const string &line)
{
	;
	doc_iter->second.add_line();
	doc_iter->second.add_line();
	//add_line = &mx_context::add_line_markup;
}

void mx_context::example(const string &line)
{
	//envs.push(make_pair("example", &mx_context::end_example));

	;
	doc_iter->second.add_line();
	doc_iter->second.add_line(".. code-block:: guess");
	doc_iter->second.add_line();
	//parse_line = &mx_context::parse_line_literal;
	//add_line = &mx_context::add_line_asis;
}

void mx_context::end_example(const string &line)
{
	;
	doc_iter->second.add_line();
	doc_iter->second.add_line();
	//add_line = &mx_context::add_line_markup;
}

void mx_context::verbatim(const string &line)
{
	//envs.push(make_pair("verbatim", &mx_context::end_verbatim));

	;
	doc_iter->second.add_line("::");
	doc_iter->second.add_line();
	doc_iter->second.set_indent(prefixes.empty() ? 3
						   : prefixes.top().second + 3);
	//parse_line = &mx_context::parse_line_literal;
	//add_line = &mx_context::add_line_asis;
}

void mx_context::end_verbatim(const string &line)
{
	;
	doc_iter->second.set_indent(prefixes.empty() ? 0 : prefixes.top().second);
	doc_iter->second.add_line();
	doc_iter->second.add_line();
	//add_line = &mx_context::add_line_markup;
}

void mx_context::itemize(const string &line)
{
	//envs.push(make_pair("itemize", &mx_context::end_itemize));
	unsigned int indent(prefixes.empty() ? 0 : prefixes.top().second
						 + 3);

	;
	doc_iter->second.add_line();
	prefixes.push(make_pair("* ", indent));
}

void mx_context::enumerate(const string &line)
{
	//envs.push(make_pair("enumerate", &mx_context::end_itemize));
	unsigned int indent(prefixes.empty() ? 0 : prefixes.top().second
						    + 3);

	;
	doc_iter->second.add_line();
	prefixes.push(make_pair("#. ", indent));
}

void mx_context::end_itemize(const string &line)
{
	;
	doc_iter->second.add_line();
	prefixes.pop();

	if (prefixes.empty())
		doc_iter->second.set_indent(0);
	else
		doc_iter->second.set_indent(prefixes.top().second);
}

/* Handled as a definition list */
void mx_context::table(const string &line)
{
	//envs.push(make_pair("table", &mx_context::end_table));
	unsigned int indent(prefixes.empty() ? 0 : prefixes.top().second
						    + 3);

	;
	doc_iter->second.add_line();
	prefixes.push(make_pair(string(), indent));
}

void mx_context::end_table(const string &line)
{
	;
	doc_iter->second.add_line();
	prefixes.pop();

	if (prefixes.empty())
		doc_iter->second.set_indent(0);
	else
		doc_iter->second.set_indent(prefixes.top().second);
}

void mx_context::multitable(const string &line)
{
	//envs.push(make_pair("multitable", &mx_context::end_multitable));
	unsigned int indent(prefixes.empty() ? 0 : prefixes.top().second
						 + 3);

	;
	doc_iter->second.add_line();
	prefixes.push(make_pair(string(), indent));
}

void mx_context::end_multitable(const string &line)
{
	;
	doc_iter->second.add_line();
	prefixes.pop();

	if (prefixes.empty())
		doc_iter->second.set_indent(0);
	else
		doc_iter->second.set_indent(prefixes.top().second);
}
/*
void mx_context::end(const string &line)
{
	if (!line.empty()) {
		if (envs.empty()
		    || (line != envs.top().first))
			throw runtime_error((boost::format("unbalanced end tag "
							   "%1% at %2%")
					     % line % in.top().location())
					    .str());

		envs.top().second(*this);
		envs.pop();
	}
}

void mx_context::item(const string &line)
{
	decltype(mx_item_tags.begin()) iter;

	if (envs.empty()
	    || (mx_item_tags.end()
		== (iter = mx_item_tags.find(envs.top().first)))) {
		cerr << "ignoring loose @item at " << in.top().location()
		     << endl;
		return;
	}

	iter->second(*this);
}

void mx_context::tab(const string &line)
{
	decltype(mx_item_tags.begin()) iter;

	if (envs.empty()
	    || (mx_item_tags.end()
		== (iter = mx_item_tags.find(envs.top().first)))) {
		cerr << "ignoring loose @tab at " << in.top().location()
		     << endl;
		return;
	}

	add_line_markup(line);
	t_iter->second.set_indent(prefixes.top().second + 6);
}


void mx_context::item_itemize()
{
	;
	t_iter->second.add_line();
	t_iter->second.set_indent(prefixes.top().second);
	//add_line_markup(prefixes.top().first + line);
	t_iter->second.set_indent(prefixes.top().second
				  + prefixes.top().first.size());
}

void mx_context::item_table()
{
	;
	t_iter->second.add_line();
	t_iter->second.set_indent(prefixes.top().second);
	//add_line_markup(prefixes.top().first + line);
	t_iter->second.set_indent(prefixes.top().second + 3);
}
*/

void mx_context::subblock(bool open)
{
	envs.push(make_pair("{", envs.top().second));
}

void mx_context::end_subblock(bool open)
{
	if ("{" != envs.top().first) {
		cerr << "unbalanced subblock end at " << in.top().location()
		     << " - ignoring." << endl;
	} else {
		envs.top().second(this, loc_t(in.top().name.filename(),
					      in.top().line_cnt + 1));
		envs.pop();
	}
}

void mx_context::subblock1(const string &line)
{
	//envs.push(make_pair("(", &mx_context::end_subblock));
	//add_line_prev = add_line;
	//add_line = &mx_context::add_line_noop;
}

void mx_context::end_subblock1(const string &line)
{
	if (envs.empty() || ("(" != envs.top().first))
		throw runtime_error((boost::format("unbalanced subblock type 1 "
						  "end at %1%.")
				     % in.top().location()).str());
	else {
		envs.pop();
		//add_line = add_line_prev;
	}
}

void mx_context::qtex(const string &line)
{
	;
/*
	t_iter->second.add_line("raw:: latex");
	t_iter->second.add_line();
	auto_end = &mx_context::end_qtex;
*/
}

void mx_context::end_qtex(const string &tag)
{
/*
	;
	t_iter->second.add_line();
	t_iter->second.add_line();
	auto_end.reset();
*/
}

void mx_context::tex(const string &line)
{
	/* Just ignore qtex tags as mostly misplaced */

	//envs.push(make_pair("tex", &mx_context::end_tex));

	;
}

void mx_context::iftex(const string &line)
{
	/* Just ignore qtex tags as mostly misplaced */

	//envs.push(make_pair("iftex", &mx_context::end_tex));

	;
}

void mx_context::end_tex(const string &line)
{
	;
}

void mx_context::ifset(const string &line)
{
	//envs.push(make_pair("ifset", &mx_context::end_if));
	;

	auto iter(defines.find(line));
	if (iter != defines.end()) {
		//parse_line = &mx_context::parse_line_doc;
		//add_line = &mx_context::add_line_markup;
	} else {
		//parse_line = &mx_context::parse_line_literal;
		//add_line = &mx_context::add_line_noop;
	}
}

void mx_context::ifclear(const string &line)
{
	//envs.push(make_pair("ifclear", &mx_context::end_if));
	;

	auto iter(defines.find(line));
	if (iter == defines.end()) {
		//parse_line = &mx_context::parse_line_doc;
		//add_line = &mx_context::add_line_markup;
	} else {
		//parse_line = &mx_context::parse_line_literal;
		//add_line = &mx_context::add_line_noop;
	}
}

void mx_context::end_if(const string &line)
{
	;
	//add_line = &mx_context::add_line_markup;
}
#if 0

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

	if (out.empty())
		out.push_back(make_pair(pad_string(indent), 0));

	while (bx::regex_search(b_iter, e_iter, what, macro_ref_expr)) {
		if (what[2].length()) {
			saved_line = make_pair(string(b_iter, what[2].first),
					       line_pos);
			return;
		}

		auto t_indent(find_print_length(what.prefix()));
		out.back().first += what.prefix();
		string m_name(what[1]);

		bx::sregex_iterator v_iter(what[1].first, what[1].second,
					   paren_expr), e_iter;

		if (v_iter != e_iter)
			m_name = v_iter->prefix();

		auto m_iter(macros.find(m_name));

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

			if (v_iter != e_iter)
				split_csv(
					m_vars,
					make_pair((*v_iter)[0].first + 1,
						  (*v_iter)[0].second - 1),
					csv_paren_expr
				);
#ifdef DEBUG_MACROS
			cerr << "t |" << what[0] << "|" << endl;
			cerr << "  m |" << m_name << "|" << endl;
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
#endif

void mx_context::emph(line_t const &line)
{
	matched_line->append("*" + line + "*");
}

void mx_context::strong(line_t const &line)
{
	matched_line->append("**" + line + "**");
}

void mx_context::verb(line_t const &line)
{
	matched_line->append("``" + line + "``");
}

void mx_context::url(line_t const &line)
{
	vector<mx_gr::storage_t> out;

	split_csv(out, mx_gr::rsub_match(line.value().begin(),
					 line.value().end(), true));

	switch (out.size()) {
	case 0:
		break;
	case 1:
		matched_line->append("`<" + out[0] + ">`");
		break;
	case 2:
		matched_line->append("`" + out[1] + "<" + out[0] + ">`");
		break;
	}
}

void mx_context::href(line_t const &line)
{
	mx_gr::rmatch what;

	if (bx::regex_match(line.value(), what, mx_gr::href)) {
		matched_line->append("`");
		if (what[2])
			matched_line->append(what[2]);

		matched_line->append("<" + what[1] + ">`_");
	} else
		matched_line->append(line);
}

void mx_context::image(line_t const &line)
{
	vector<mx_gr::storage_t> out;

	split_csv(out, mx_gr::rsub_match(line.value().begin(),
					 line.value().end(), true));

	if (out.size()) {
		string i_ref(
			"|image_" + boost::lexical_cast<string>(image_cnt++)
			+ '|'
		);

		extra_lines.push_back(
			".. " + i_ref + " image:: " + out[0] + ".*"
		);
		matched_line->append(i_ref);
	}
}

void mx_context::cite(line_t const &line)
{
	matched_line->append("[" + line + "]_");
	extra_lines.push_back(".. [" + line + "] Insert citation body here.");
}

void mx_context::set_literal1(mx_gr::rsub_match const &expr,
			      mx_gr::rsub_match const &body,
			      mx_gr::rsub_match const &tail)
{
	matched = true;

	append_lines(mx_gr::rsub_match(in.top().range.first, expr.first, true));

	string sel(1, *(expr.first + 1));

	if (sel[0] == '\'')
		new_line();
	else {
		if (!matched_line)
			new_line();

		mx_gr::storage_t b_line;
		in.top().line_cnt += unbreak_line(b_line, body);

		auto iter(mx_literal_tags.find(sel));
		if (iter != mx_literal_tags.end()) {
			iter->second(this, b_line);

			if (tail.length()) {
				if (isblank(*(tail.first)))
					matched_line->append(tail);
				else
					new_line();
			} else
				matched_line->append("\\ ");
		} else
			append_lines(expr);
	}
}

void mx_context::set_literal2(mx_gr::rsub_match const &expr,
			      mx_gr::rsub_match const &body,
			      mx_gr::rsub_match const &tail)
{
	matched = true;

	append_lines(mx_gr::rsub_match(in.top().range.first, expr.first, true));

	if (!matched_line)
		new_line();

	mx_gr::storage_t b_line;
	in.top().line_cnt += unbreak_line(
		b_line,
		mx_gr::rsub_match(body.first + 1, body.second - 1, true)
	);

	string sel(expr.first + 1, body.first);

	auto iter(mx_literal_tags.find(sel));
	if (iter != mx_literal_tags.end()) {
		iter->second(this, b_line);

		if (tail.length()) {
			if (isblank(*(tail.first)))
				matched_line->append(tail);
			else
				new_line();
		} else
			matched_line->append("\\ ");
	} else
		append_lines(expr);
}

void mx_context::set_index_text(mx_gr::rsub_match const &expr,
			        mx_gr::rsub_match const &body,
			        mx_gr::rsub_match const &tail)
{
	matched = true;

	append_lines(mx_gr::rsub_match(in.top().range.first, expr.first,
		     true));

	if (!matched_line)
		new_line();

	matched_line->append(body);

	if (tail.length()) {
		if (isblank(*(tail.first)))
			matched_line->append(tail);
		else
			new_line();
	}
}

void mx_context::set_index_doc(mx_gr::rsub_match const &expr,
			       mx_gr::rsub_match const &body,
			       mx_gr::rsub_match const &tail)
{
	matched = true;

	append_lines(mx_gr::rsub_match(in.top().range.first, expr.first,
		     true));

	if (!matched_line)
		new_line();

	matched_line->append("``" + body + "``");

	if (tail.length()) {
		if (isblank(*(tail.first)))
			matched_line->append(tail);
		else
			new_line();
	} else
		matched_line->append("\\ ");
}

void mx_context::add_macro_line(mx_gr::rsub_match const &expr)
{
	matched = true;

	append_lines(mx_gr::rsub_match(in.top().range.first, expr.first,
		     true));

	if (!matched_line)
		new_line();

	mx_gr::storage_t t_line;
	auto l_cnt(unbreak_line(t_line, expr));

	matched_line->append(line_t(t_line, in.top().location()));

	if (l_cnt) {
		new_line();
		in.top().line_cnt += l_cnt - 1;
	}
}

static mx_gr::storage_t replace_macro_args(vector<mx_gr::storage_t> const &args,
					   mx_gr::rmatch const &what)
{
	size_t idx;

	switch (*(what[0].first + 1)) {
	case ':': {
		auto l_out(
			regex_replace<mx_gr::storage_t>(
				what[0].first + 1,
				what[0].second,
				mx_gr::macro_tags,
				bind(replace_macro_args, cref(args), _1)
			)
		);

		l_out.push_front('@');
		return l_out;
	}
	case '?': {
		mx_gr::rmatch what_arg;
		auto a_iter((*what.nested_results().begin())[1].first - 1);

		if (bx::regex_match(what[0].first + 2, a_iter, what_arg,
				    mx_gr::macro_arg))
			idx = boost::lexical_cast<size_t>(what_arg[1]);
		else
			idx = 0;

		if ((idx < 1) || (idx > args.size()) || args[idx].empty())
			return mx_gr::storage_t();

		auto l_out(
			regex_replace<mx_gr::storage_t>(
				a_iter,
				what[0].second,
				mx_gr::macro_tags,
				bind(&replace_macro_args, cref(args), _1)
			)
		);

		l_out.push_front('@');
		return l_out;
	}
	default:
		mx_gr::storage_t l_int(
			(*what.nested_results().begin())[1].first,
			(*what.nested_results().begin())[1].second
		);

		idx = boost::lexical_cast<size_t>(l_int);

		if ((idx < 1) || (idx > args.size()))
			return mx_gr::storage_t(what[0].first, what[0].second);
		else
			return args[idx - 1];
	}
}

void mx_context::expand_macro_r(line_list_t &m_exp,
				decltype(macros.begin()) m_iter,
				vector<mx_gr::storage_t> const &args,
				size_t indent)
{
	mx_gr::actions dummy;
	mx_gr::rmatch what;
	what.let(mx_gr::_actions = dummy);

	if (m_exp.empty())
		m_exp.push_back(line_t(pad_string(indent)));

	BOOST_FOREACH(auto const &line, m_iter->second.lines) {
		loc_t m_loc(m_iter->second.loc.first, line.line_pos());

		auto l_out(
			regex_replace<mx_gr::storage_t>(
				line.value().begin(),
				line.value().end(),
				mx_gr::macro_tags,
				bind(replace_macro_args, cref(args), _1)
			)
		);

		auto b_iter(l_out.begin()), e_iter(l_out.end());

		while (bx::regex_search(b_iter, e_iter, what,
					mx_gr::macro_ref)) {

			auto t_indent(find_print_length(what.prefix().first,
							what.prefix().second));
			m_exp.back().append(what.prefix());

			string m_name(what[1].first, what[1].second);
			auto m_out_iter(macros.find(m_name));

			if (m_out_iter == macros.end()) {
				if (macro_pass > 1)
					cerr << "pass " << macro_pass
					     << ": undefined macro "
					     << what[1] << ", ignoring for now"
					     << endl;

				m_exp.back().append(line_t(what[0], m_loc));
				continue;
			}

			m_exp.push_back(m_loc);

			vector<mx_gr::storage_t> out_args;

			if (what[2].length()) {
				split_csv(
					out_args,
					mx_gr::rsub_match(
						what[2].first + 1,
						what[2].second - 1,
						true
					),
					mx_gr::paren_csv
				);
			}

			expand_macro_r(m_exp, m_out_iter, out_args,
				       indent + t_indent);
		}

		if (b_iter != e_iter)
			m_exp.back().append(
				mx_gr::rsub_match(b_iter, e_iter, true)
			);

		m_exp.push_back(pad_string(indent));
	}
}

void mx_context::expand_macro(mx_gr::rsub_match const &expr,
			      mx_gr::rsub_match const &name,
			      mx_gr::rsub_match const &arg)
{
	string m_name(name.first, name.second);
	auto m_iter(macros.find(m_name));

	if (m_iter == macros.end()) {
		add_macro_line(expr);
		return;
	}

	matched = true;

	append_lines(mx_gr::rsub_match(in.top().range.first, expr.first,
		     true));

	if (!matched_line)
		new_line();

	mx_gr::storage_t t_line;
	in.top().line_cnt += unbreak_line(t_line, expr);

	vector<mx_gr::storage_t> args;

	if (arg.length()) {
		t_line.clear();
		unbreak_line(t_line, arg);

		split_csv(
			args,
			mx_gr::rsub_match(t_line.begin() + 1,
					  t_line.end() - 1, true),
			mx_gr::paren_csv
		);
	}

	line_list_t m_exp;

	expand_macro_r(m_exp, m_iter, args,
		       find_print_length(matched_line->value().begin(),
					 matched_line->value().end()));

	if (!m_exp.empty()) {
		matched_line->append(m_exp.front());
		m_exp.pop_front();
		t_iter->second.add_line(*matched_line);
		matched_line.reset();
	}

	if (!m_exp.empty()) {
		matched_line = m_exp.back();
		m_exp.pop_back();
	}

	BOOST_FOREACH(auto const &l, m_exp)
		t_iter->second.add_line(l);
}

void mx_context::set_tag(mx_gr::rsub_match const &expr,
			 mx_gr::rsub_match const &tag,
			 mx_gr::rsub_match const &nline)
{
	matched = true;

	append_lines(mx_gr::rsub_match(in.top().range.first, expr.first, true));

	if (!tag) {
		in.top().reset_tag(this);
		if (nline)
			new_line();
	} else {
		string s_tag(tag);
		auto tag_iter(mx_tags.find(s_tag));

		if (tag_iter != mx_tags.end()) {
			if (!tag_iter->second.test(tag_t::NONTERM))
				in.top().reset_tag(this);

			if ((in.size() == 1)
			    || tag_iter->second.test(tag_t::INCFILE)) {
				if (!tag_iter->second.test(tag_t::ONESHOT))
					in.top().set_tag(this, tag_iter->second,
							 s_tag);
				else
					tag_iter->second.handler(this, true);
			}
		} else {
			in.top().reset_tag(this);

			if (in.size() == 1)
				in.top().set_tag(
					this, tag_t(
						mx_gr::text_tags,
						make_shared<text_actions>(),
						&mx_context::generic_tag
					), s_tag
				);
		}
	}
}

void mx_context::basic_actions::macro_tag(mx_gr::rsub_match const &expr,
					  mx_gr::rsub_match const &name,
					  mx_gr::rsub_match const &arg) const
{
	ctx->add_macro_line(expr);
}


void mx_context::basic_actions::control_tag(mx_gr::rsub_match const &s1,
					    mx_gr::rsub_match const &s2,
					    mx_gr::rsub_match const &s3) const
{
	ctx->set_tag(s1, s2, s3);
}

void mx_context::text_actions::index_tag(mx_gr::rsub_match const &expr,
					 mx_gr::rsub_match const &body,
					 mx_gr::rsub_match const &tail) const
{
	ctx->set_index_text(expr, body, tail);
}

void mx_context::text_actions::macro_tag(mx_gr::rsub_match const &expr,
					 mx_gr::rsub_match const &name,
					 mx_gr::rsub_match const &arg) const
{
	ctx->expand_macro(expr, name, arg);
}


void mx_context::doc_actions::literal1_tag(mx_gr::rsub_match const &expr,
					   mx_gr::rsub_match const &body,
					   mx_gr::rsub_match const &tail) const
{
	ctx->set_literal1(expr, body, tail);
}

void mx_context::doc_actions::literal2_tag(mx_gr::rsub_match const &expr,
					   mx_gr::rsub_match const &body,
					   mx_gr::rsub_match const &tail) const
{
	ctx->set_literal2(expr, body, tail);
}

void mx_context::doc_actions::index_tag(mx_gr::rsub_match const &expr,
					mx_gr::rsub_match const &body,
					mx_gr::rsub_match const &tail) const
{
	ctx->set_index_doc(expr, body, tail);
}


mx_context::mx_context(const vector<string> &includes_,
		       const string &doc_tag,
		       const set<string> &defines_)
	   :end_pos(0),
	    doc_iter(out_files.insert(make_pair(doc_tag,
						target(doc_tag))).first),
	    t_iter(doc_iter),
	    includes(includes_.begin(), includes_.end()),
	    defines(defines_),
	    min_sec_lvl(-1),
	    abs_sec_lvl(TITLE_LVL),
	    rel_sec_lvl(TITLE_LVL),
	    image_cnt(0),
	    macro_pass(1),
	    modulename_set(false),
	    matched(false)
{
	string t_str;

	/* "includes" is supposed to contain directory pathes, first member is
	 * an exception, containing the actual input file path.
	 */
	in.push(in_file(bf::system_complete(includes_[0]),
		tag_t(mx_gr::doc_tags, make_shared<doc_actions>(this),
		      &mx_context::null_tag)));
	envs.push(make_pair(string(), &mx_context::add_line_default));

	/* Now, it's pathes all the way down. */
	includes[0].remove_filename();
	mx_gr::rmatch what;

	while (true) {
		auto &range(in.top().range);
		what.let(mx_gr::_actions = *(in.top().actions()));

		if (bx::regex_search(range.first, range.second, what,
				     in.top().expr())) {

			if (!matched)
				append_lines(
					mx_gr::rsub_match(range.first,
							  what[0].second,
							  true)
				);

			matched = false;
			range.first = what.suffix().first;
		} else {
			append_lines(range);
			range.first = range.second;
			in.top().reset_tag(this);
		}

		if (range.length())
			continue;

		new_line();

		if (in.size() == 1)
			break;
		else if (in.size() == 2) {
			bf::path i_path(in.top().name.parent_path()
						.file_string());
			string i_base(in.top().base_name);

			in.pop();

			bf::path rel_path(
				find_relative(
					i_path, in.top().name.parent_path()
							.file_string()
				)
			);

			rel_path /= i_base;

			doc_iter->second.add_line();
			doc_iter->second.add_line(
				(boost::format("Include :doc:`%1%`.")
				 % rel_path.file_string()).str()
			);
		} else
			in.pop();
	}
/*
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
*/
	while (!extra_lines.empty()) {
		doc_iter->second.add_line(extra_lines.front());
		extra_lines.pop_front();
	}
}

void mx_context::write_out(const bf::path &prefix)
{
	ofstream ofile;
	bf::path f_path(prefix / in.top().base_name);

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
		cout << "mx2sphinx version 2.0" << endl;
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
			toc.back().name = mx.in.top().base_name;
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
