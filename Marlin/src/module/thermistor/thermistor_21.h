/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#pragma once

#define REVERSE_TEMP_SENSOR_RANGE_21 1

// Pt100 with AD8227 amp
constexpr temp_entry_t temptable_21[] PROGMEM = {
  { OV(196),    0 },
  { OV(208),    9 },
  { OV(229),   38 },
  { OV(249),   71 }, 
  { OV(269),  103 },
  { OV(289),  136 },
  { OV(329),  203 },
  { OV(369),  273 },
  { OV(407),  342 },
  { OV(445),  416 },
  { OV(481),  479 },
  { OV(518),  580 }
};
