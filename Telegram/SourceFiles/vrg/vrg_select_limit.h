#pragma once

#include "config.h"
#include "vrg/vrg_conf.h"

namespace Vrg {

[[nodiscard]] inline int SelectLimit() {
	return Conf::Bool("select_unlimited", true) ? 100000 : MaxSelectedItems;
}

}

