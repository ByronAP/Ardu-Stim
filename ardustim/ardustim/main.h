/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * ArduStim - Arbitrary wheel pattern generator definitions
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
#ifndef __ARDUSTIM_H__
#define __ARDUSTIM_H__

#include <stdint.h>
#include <Arduino.h>

// Function prototypes
/**
 * @brief Reset timer compare value for a new RPM
 * @param rpm Target RPM value
 */
void resetNewOCR1A(uint32_t rpm);

/**
 * @brief Sets the target RPM
 * @param newRPM New RPM value
 */
void setRPM(uint16_t newRPM);

/**
 * @brief Calculate compression effect on RPM
 * @return Modifier value for RPM
 */
uint16_t calculateCompressionModifier();

/**
 * @brief Calculate current crank angle
 * @return Angle in degrees (0-360)
 */
uint16_t calculateCurrentCrankAngle();

/**
 * @brief Initialize the lookahead cache
 * @param startEdge First edge index to cache
 */
void initLookaheadCache(uint16_t startEdge);

/**
 * @brief Update RPM based on current mode
 * @param rpm Pointer to RPM value to update
 */
void updateRpmBasedOnMode(uint16_t *rpm);

/**
 * @brief Validates configuration values
 * @param config Pointer to configuration structure
 */
void validateConfiguration(struct configTable *config);

#endif