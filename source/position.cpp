////////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
////////////////////////////////////////////////////////////////////////
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
////////////////////////////////////////////////////////////////////////
#include "otpch.h"
#include <iomanip>
#include "position.h"

std::ostream& operator<<(std::ostream& os, const Position& pos)
{
	os << "( " << std::setw(5) << std::setfill('0') << pos.x;
	os << " / " << std::setw(5) << std::setfill('0') << pos.y;
	os << " / " << std::setw(3) << std::setfill('0') << pos.z;
	os << " )";
	return os;
}

std::ostream& operator<<(std::ostream& os, const Direction& dir)
{
	switch(dir)
	{
		case NORTH:
			os << "Norte";
			break;
		case EAST:
			os << "Leste";
			break;
		case WEST:
			os << "Oeste";
			break;
		case SOUTH:
			os << "Sul";
			break;
		case SOUTHWEST:
			os << "Sudoestet";
			break;
		case SOUTHEAST:
			os << "Sudeste";
			break;
		case NORTHWEST:
			os << "Noroeste";
			break;
		case NORTHEAST:
			os << "Nordeste";
			break;
	}

	return os;
}
