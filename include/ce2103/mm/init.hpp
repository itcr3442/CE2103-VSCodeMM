#ifndef CE2103_INIT_HPP
#define CE2103_INIT_HPP

namespace ce2103::mm
{
	//! Initializes the library for local operation, ignoring network hints.
	void initialize_local();

	//! Initializes the library for local and (according to hints) remote operation.
	void initialize();
}

#endif
