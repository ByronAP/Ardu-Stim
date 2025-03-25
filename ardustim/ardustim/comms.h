/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - Serial communications handling
 *
 * Copyright 2014 David J. Andruczyk
 * 
 * Ardu-Stim software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ArduStim software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with any ArduStim software.  If not, see http://www.gnu.org/licenses/
 *
 */
#ifndef __COMMS_H__
#define __COMMS_H__
 
#include <Arduino.h>

// Prototypes for command handlers
/**
 * @brief Parse and process incoming commands from a stream
 * @param stream The Stream object to read from and write to
 * @return bool True if a command was processed
 */
bool commandParser(Stream* stream);

/**
 * @brief Get a string representation of the board type
 * @return const char* String with board type
 */
const char* getBoardTypeString();

// Command callbacks
void selectNextWheelCb();       // Selects the next wheel pattern
void selectPreviousWheelCb();   // Selects the previous wheel pattern
void toggleInvertPrimaryCb();   // Toggles inversion of primary output
void toggleInvertSecondaryCb(); // Toggles inversion of secondary output
void displayNewWheel();         // Updates system for new wheel selection

// Utility functions
uint32_t freeRam();             // Returns amount of free RAM

#endif