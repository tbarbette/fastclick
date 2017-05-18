#include <click/config.h>
#include "groupregexclassifier.hh"
#include <click/glue.hh>
#include <click/error.hh>
#include <click/args.hh>
#include <click/confparse.hh>
#include <click/router.hh>

CLICK_DECLS

GroupRegexClassifier::GroupRegexClassifier() {
}

GroupRegexClassifier::~GroupRegexClassifier() {
}

int
GroupRegexClassifier::configure(Vector<String> &conf, ErrorHandler *errh)
{
	String pattern, group;
	int group_number;
	Vector<int> seen_groups(noutputs(), 0);
	for (int i=0; i < conf.size(); ++i) {
		cp_string(conf[i], &pattern, &group);
		group_number = atoi(group.c_str());
		if (group_number > noutputs()) {
			return errh->error("Error with pattern %d, group number is larger then noutputs", i);
		}

		_patterns.push_back(pattern);
		_patterns_group_number.push_back(group_number);
		seen_groups[group_number]++;
	}

	for (int i=0; i<seen_groups.size(); ++i) {
		if (seen_groups[i] == 0) {
			return errh->error("Group %d has no patterns", i);
		}
	}

	_groups_count = seen_groups;

	if (!is_valid_patterns(_patterns, errh)) {
		return -1;
	}

	if (!_program.is_open()) {
		_program.reset();
	}

	for (int i=0; i<_patterns.size(); ++i) {
		int result = _program.add_pattern(_patterns[i]);
		if (result < 0) {
			// This should not happen
			return errh->error("Error (%d) adding pattern %d: %s", result, i, pattern.c_str());
		}
	}

	if (!_program.compile()) {
		// This should not happen
		return errh->error("Unable to compile patterns");
	}

	if (!errh->nerrors()) {
		return 0;
	} else {
		return -1;
	}
}

bool
GroupRegexClassifier::is_valid_patterns(Vector<String> &patterns, ErrorHandler *errh) const{
	RegexSet test_set;
	bool valid = true;
	for (int i=0; i < patterns.size(); ++i) {
		String pattern = cp_unquote(patterns[i]);
		int result = test_set.add_pattern(pattern);
		if (result < 0) {
			errh->error("Error (%d) in pattern %d: %s", result, i, pattern.c_str());
			valid = false;
		}
	}
	if (valid) {
		// Try to compile
		valid = test_set.compile();
	}

	return valid;
}


void
GroupRegexClassifier::add_handlers() {
	for (uintptr_t i = 0; i != (uintptr_t) noutputs(); ++i) {
		add_read_handler("pattern" + String(i), read_positional_handler, (void*) i);
		add_write_handler("pattern" + String(i), reconfigure_positional_handler, (void*) i);
	}
}

void
GroupRegexClassifier::push(int port, Packet *p) {
	char *data = (char *) p->data();
	int length = p->length();
	checked_output_push(_program.match_group(data, length, _patterns_group_number, _groups_count), p);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(GroupRegexClassifier)
ELEMENT_REQUIRES(userlevel RegexSet)
ELEMENT_MT_SAFE(GroupRegexClassifier)
