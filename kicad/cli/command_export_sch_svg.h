/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2022 Mark Roszko <mark.roszko@gmail.com>
 * Copyright (C) 1992-2022 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COMMAND_EXPORT_SCH_SVG_H
#define COMMAND_EXPORT_SCH_SVG_H

#include "command_export_pcb_base.h"

namespace CLI
{
class EXPORT_SCH_SVG_COMMAND : public EXPORT_PCB_BASE_COMMAND
{
public:
    EXPORT_SCH_SVG_COMMAND();

    int Perform( KIWAY& aKiway ) override;
};
} // namespace CLI

#endif