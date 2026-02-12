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

#define REVERSE_TEMP_SENSOR_RANGE_20 1

// Pt100 with INA826 amp
constexpr temp_entry_t temptable_20[] PROGMEM = {
  { OV(196),    0 },
  { OV(208),   17 },
  { OV(229),   48 },
  { OV(249),   80 }, 
  { OV(269),  114 },
  { OV(289),  147 },
  { OV(329),  217 },
  { OV(369),  290 },
  { OV(407),  363 },
  { OV(445),  438 },
  { OV(481),  510 },
  { OV(518),  585 }
};
