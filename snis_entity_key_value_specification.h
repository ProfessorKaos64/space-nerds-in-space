#ifndef SNIS_ENTITY_KEY_VALUE_SPECIFICATION_H__
#define SNIS_ENTITY_KEY_VALUE_SPECIFICATION_H__

/* Macros for constructing key value specifications for snis_entity, and snis_entity.tsd.ship */
#define DOUBLE_FIELD(x) { #x, KVS_DOUBLE, 0, offsetof(struct snis_entity, x), sizeof(double), }
#define FLOAT_FIELD(x) { #x, KVS_FLOAT, 0, offsetof(struct snis_entity, x), sizeof(float), }
#define UINT32_FIELD(x) { #x, KVS_UINT32, 0, offsetof(struct snis_entity, x), sizeof(uint32_t), }
#define UINT16_FIELD(x) { #x, KVS_UINT16, 0, offsetof(struct snis_entity, x), sizeof(uint16_t), }
#define UINT8_FIELD(x) { #x, KVS_INT8, 0, offsetof(struct snis_entity, x), sizeof(int8_t), }
#define INT32_FIELD(x) { #x, KVS_INT32, 0, offsetof(struct snis_entity, x), sizeof(int32_t), }
#define INT16_FIELD(x) { #x, KVS_INT16, 0, offsetof(struct snis_entity, x), sizeof(int16_t), }
#define INT8_FIELD(x) { #x, KVS_INT8, 0, offsetof(struct snis_entity, x), sizeof(int8_t), }
#define STRING_FIELD(x) { #x, KVS_STRING, 0, offsetof(struct snis_entity, x), \
	sizeof(((struct snis_entity *) 0)->x), }

#define FLOAT_TSDFIELD(x) { #x, KVS_FLOAT, 0, offsetof(struct snis_entity, tsd) + \
				offsetof(struct ship_data, x), sizeof(float), }
#define DOUBLE_TSDFIELD(x) { #x, KVS_DOUBLE, 0, offsetof(struct snis_entity, tsd) + \
				offsetof(struct ship_data, x), sizeof(double), }
#define UINT32_TSDFIELD(x) { #x, KVS_UINT32, 0, offsetof(struct snis_entity, tsd) + \
				offsetof(struct ship_data, x), sizeof(uint32_t), }
#define UINT16_TSDFIELD(x) { #x, KVS_UINT16, 0, offsetof(struct snis_entity, tsd) + \
				offsetof(struct ship_data, x), sizeof(uint16_t), }
#define UINT8_TSDFIELD(x) { #x, KVS_UINT8, 0, offsetof(struct snis_entity, tsd) + \
				offsetof(struct ship_data, x), sizeof(uint8_t), }
#define INT32_TSDFIELD(x) { #x, KVS_INT32, 0, offsetof(struct snis_entity, tsd) + \
				offsetof(struct ship_data, x), sizeof(int32_t), }
#define INT16_TSDFIELD(x) { #x, KVS_INT16, 0, offsetof(struct snis_entity, tsd) + \
				offsetof(struct ship_data, x), sizeof(int16_t), }
#define INT8_TSDFIELD(x) { #x, KVS_INT8, 0, offsetof(struct snis_entity, tsd) + \
				offsetof(struct ship_data, x), sizeof(int8_t), }
#define STRING_TSDFIELD(x) { #x, KVS_STRING, 0, offsetof(struct snis_entity, tsd) + \
				offsetof(struct ship_data, x), sizeof(((struct snis_entity *) 0)->tsd.ship.x) }
#ifdef INCLUDE_BRIDGE_INFO_FIELDS
#define INT32_BRIDGE_FIELD(x) { #x, KVS_INT32, 1, offsetof(struct bridge_info, x), sizeof(int32_t), }
#endif

struct key_value_specification snis_entity_kvs[] = {
	DOUBLE_FIELD(x),
	DOUBLE_FIELD(y),
	DOUBLE_FIELD(z),
	DOUBLE_FIELD(vx),
	DOUBLE_FIELD(vy),
	DOUBLE_FIELD(vz),
	DOUBLE_FIELD(heading),
	UINT16_FIELD(alive),
	UINT32_FIELD(type),
	UINT32_TSDFIELD(torpedoes),
	UINT32_TSDFIELD(power),
	UINT32_TSDFIELD(shields),
	STRING_TSDFIELD(shipname),
	DOUBLE_TSDFIELD(velocity),
	DOUBLE_TSDFIELD(yaw_velocity),
	DOUBLE_TSDFIELD(pitch_velocity),
	DOUBLE_TSDFIELD(roll_velocity),
	DOUBLE_TSDFIELD(desired_velocity),
	DOUBLE_TSDFIELD(gun_yaw_velocity),
	DOUBLE_TSDFIELD(sci_heading),
	DOUBLE_TSDFIELD(sci_beam_width),
	DOUBLE_TSDFIELD(sci_yaw_velocity),
	FLOAT_TSDFIELD(sciball_orientation.vec[0]),
	FLOAT_TSDFIELD(sciball_orientation.vec[1]),
	FLOAT_TSDFIELD(sciball_orientation.vec[2]),
	FLOAT_TSDFIELD(sciball_orientation.vec[3]),
	DOUBLE_TSDFIELD(sciball_yawvel),
	DOUBLE_TSDFIELD(sciball_pitchvel),
	DOUBLE_TSDFIELD(sciball_rollvel),
	FLOAT_TSDFIELD(weap_orientation.vec[0]),
	FLOAT_TSDFIELD(weap_orientation.vec[1]),
	FLOAT_TSDFIELD(weap_orientation.vec[2]),
	FLOAT_TSDFIELD(weap_orientation.vec[3]),
	DOUBLE_TSDFIELD(weap_yawvel),
	DOUBLE_TSDFIELD(weap_pitchvel),
	UINT8_TSDFIELD(torpedoes_loaded),
	UINT8_TSDFIELD(torpedoes_loading),
	UINT16_TSDFIELD(torpedo_load_time),
	UINT8_TSDFIELD(phaser_bank_charge),
	UINT32_TSDFIELD(fuel),
	UINT32_TSDFIELD(oxygen),
	UINT8_TSDFIELD(rpm),
	UINT8_TSDFIELD(throttle),
	UINT8_TSDFIELD(temp),
	UINT8_TSDFIELD(shiptype),
	UINT8_TSDFIELD(scizoom),
	UINT8_TSDFIELD(weapzoom),
	UINT8_TSDFIELD(mainzoom),
	UINT8_TSDFIELD(requested_warpdrive),
	UINT8_TSDFIELD(requested_shield),
	UINT8_TSDFIELD(phaser_wavelength),
	UINT8_TSDFIELD(phaser_charge),
	UINT8_TSDFIELD(damage.shield_damage),
	UINT8_TSDFIELD(damage.impulse_damage),
	UINT8_TSDFIELD(damage.warp_damage),
	UINT8_TSDFIELD(damage.maneuvering_damage),
	UINT8_TSDFIELD(damage.phaser_banks_damage),
	UINT8_TSDFIELD(damage.sensors_damage),
	UINT8_TSDFIELD(damage.comms_damage),
	UINT8_TSDFIELD(damage.tractor_damage),
	UINT8_TSDFIELD(damage.lifesupport_damage),
	/* TODO damcon data... */
	UINT8_TSDFIELD(view_mode),
	DOUBLE_TSDFIELD(view_angle),

	/* Beginning of power_data */
	UINT8_TSDFIELD(power_data.maneuvering.r1),
	UINT8_TSDFIELD(power_data.maneuvering.r2),
	UINT8_TSDFIELD(power_data.maneuvering.r3),
	UINT8_TSDFIELD(power_data.maneuvering.i),

	UINT8_TSDFIELD(power_data.warp.r1),
	UINT8_TSDFIELD(power_data.warp.r2),
	UINT8_TSDFIELD(power_data.warp.r3),
	UINT8_TSDFIELD(power_data.warp.i),

	UINT8_TSDFIELD(power_data.impulse.r1),
	UINT8_TSDFIELD(power_data.impulse.r2),
	UINT8_TSDFIELD(power_data.impulse.r3),
	UINT8_TSDFIELD(power_data.impulse.i),

	UINT8_TSDFIELD(power_data.sensors.r1),
	UINT8_TSDFIELD(power_data.sensors.r2),
	UINT8_TSDFIELD(power_data.sensors.r3),
	UINT8_TSDFIELD(power_data.sensors.i),

	UINT8_TSDFIELD(power_data.comms.r1),
	UINT8_TSDFIELD(power_data.comms.r2),
	UINT8_TSDFIELD(power_data.comms.r3),
	UINT8_TSDFIELD(power_data.comms.i),

	UINT8_TSDFIELD(power_data.phasers.r1),
	UINT8_TSDFIELD(power_data.phasers.r2),
	UINT8_TSDFIELD(power_data.phasers.r3),
	UINT8_TSDFIELD(power_data.phasers.i),

	UINT8_TSDFIELD(power_data.shields.r1),
	UINT8_TSDFIELD(power_data.shields.r2),
	UINT8_TSDFIELD(power_data.shields.r3),
	UINT8_TSDFIELD(power_data.shields.i),

	UINT8_TSDFIELD(power_data.tractor.r1),
	UINT8_TSDFIELD(power_data.tractor.r2),
	UINT8_TSDFIELD(power_data.tractor.r3),
	UINT8_TSDFIELD(power_data.tractor.i),

	UINT8_TSDFIELD(power_data.lifesupport.r1),
	UINT8_TSDFIELD(power_data.lifesupport.r2),
	UINT8_TSDFIELD(power_data.lifesupport.r3),
	UINT8_TSDFIELD(power_data.lifesupport.i),

	UINT8_TSDFIELD(power_data.voltage),
	/* End of power_data */

	/* Beginning of coolant_data */
	UINT8_TSDFIELD(coolant_data.maneuvering.r1),
	UINT8_TSDFIELD(coolant_data.maneuvering.r2),
	UINT8_TSDFIELD(coolant_data.maneuvering.r3),
	UINT8_TSDFIELD(coolant_data.maneuvering.i),

	UINT8_TSDFIELD(coolant_data.warp.r1),
	UINT8_TSDFIELD(coolant_data.warp.r2),
	UINT8_TSDFIELD(coolant_data.warp.r3),
	UINT8_TSDFIELD(coolant_data.warp.i),

	UINT8_TSDFIELD(coolant_data.impulse.r1),
	UINT8_TSDFIELD(coolant_data.impulse.r2),
	UINT8_TSDFIELD(coolant_data.impulse.r3),
	UINT8_TSDFIELD(coolant_data.impulse.i),

	UINT8_TSDFIELD(coolant_data.sensors.r1),
	UINT8_TSDFIELD(coolant_data.sensors.r2),
	UINT8_TSDFIELD(coolant_data.sensors.r3),
	UINT8_TSDFIELD(coolant_data.sensors.i),

	UINT8_TSDFIELD(coolant_data.comms.r1),
	UINT8_TSDFIELD(coolant_data.comms.r2),
	UINT8_TSDFIELD(coolant_data.comms.r3),
	UINT8_TSDFIELD(coolant_data.comms.i),

	UINT8_TSDFIELD(coolant_data.phasers.r1),
	UINT8_TSDFIELD(coolant_data.phasers.r2),
	UINT8_TSDFIELD(coolant_data.phasers.r3),
	UINT8_TSDFIELD(coolant_data.phasers.i),

	UINT8_TSDFIELD(coolant_data.shields.r1),
	UINT8_TSDFIELD(coolant_data.shields.r2),
	UINT8_TSDFIELD(coolant_data.shields.r3),
	UINT8_TSDFIELD(coolant_data.shields.i),

	UINT8_TSDFIELD(coolant_data.tractor.r1),
	UINT8_TSDFIELD(coolant_data.tractor.r2),
	UINT8_TSDFIELD(coolant_data.tractor.r3),
	UINT8_TSDFIELD(coolant_data.tractor.i),

	UINT8_TSDFIELD(coolant_data.lifesupport.r1),
	UINT8_TSDFIELD(coolant_data.lifesupport.r2),
	UINT8_TSDFIELD(coolant_data.lifesupport.r3),
	UINT8_TSDFIELD(coolant_data.lifesupport.i),

	UINT8_TSDFIELD(coolant_data.voltage),
	/* End of coolant_data */

	UINT8_TSDFIELD(temperature_data.shield_damage),
	UINT8_TSDFIELD(temperature_data.impulse_damage),
	UINT8_TSDFIELD(temperature_data.warp_damage),
	UINT8_TSDFIELD(temperature_data.maneuvering_damage),
	UINT8_TSDFIELD(temperature_data.phaser_banks_damage),
	UINT8_TSDFIELD(temperature_data.sensors_damage),
	UINT8_TSDFIELD(temperature_data.comms_damage),
	UINT8_TSDFIELD(temperature_data.tractor_damage),
	UINT8_TSDFIELD(temperature_data.lifesupport_damage),
	DOUBLE_TSDFIELD(warp_time),
	DOUBLE_TSDFIELD(scibeam_a1),
	DOUBLE_TSDFIELD(scibeam_a2),
	DOUBLE_TSDFIELD(scibeam_range),
	UINT8_TSDFIELD(reverse),
	UINT8_TSDFIELD(trident),
	DOUBLE_TSDFIELD(next_torpedo_time),
	DOUBLE_TSDFIELD(next_laser_time),
	UINT8_TSDFIELD(lifeform_count),
	UINT32_TSDFIELD(tractor_beam),
	UINT8_TSDFIELD(overheating_damage_done),
	FLOAT_TSDFIELD(steering_adjustment.vec[0]),
	FLOAT_TSDFIELD(steering_adjustment.vec[1]),
	FLOAT_TSDFIELD(steering_adjustment.vec[2]),
	FLOAT_TSDFIELD(braking_factor),

	FLOAT_TSDFIELD(cargo[0].paid),
	INT32_TSDFIELD(cargo[0].contents.item),
	FLOAT_TSDFIELD(cargo[0].contents.qty),
	UINT32_TSDFIELD(cargo[0].origin),
	UINT32_TSDFIELD(cargo[0].dest),
	INT32_TSDFIELD(cargo[0].due_date),

	FLOAT_TSDFIELD(cargo[1].paid),
	INT32_TSDFIELD(cargo[1].contents.item),
	FLOAT_TSDFIELD(cargo[1].contents.qty),
	UINT32_TSDFIELD(cargo[1].origin),
	UINT32_TSDFIELD(cargo[1].dest),
	INT32_TSDFIELD(cargo[1].due_date),

	FLOAT_TSDFIELD(cargo[2].paid),
	INT32_TSDFIELD(cargo[2].contents.item),
	FLOAT_TSDFIELD(cargo[2].contents.qty),
	UINT32_TSDFIELD(cargo[2].origin),
	UINT32_TSDFIELD(cargo[2].dest),
	INT32_TSDFIELD(cargo[2].due_date),

	FLOAT_TSDFIELD(cargo[3].paid),
	INT32_TSDFIELD(cargo[3].contents.item),
	FLOAT_TSDFIELD(cargo[3].contents.qty),
	UINT32_TSDFIELD(cargo[3].origin),
	UINT32_TSDFIELD(cargo[3].dest),
	INT32_TSDFIELD(cargo[3].due_date),

	FLOAT_TSDFIELD(cargo[4].paid),
	INT32_TSDFIELD(cargo[4].contents.item),
	FLOAT_TSDFIELD(cargo[4].contents.qty),
	UINT32_TSDFIELD(cargo[4].origin),
	UINT32_TSDFIELD(cargo[4].dest),
	INT32_TSDFIELD(cargo[4].due_date),

	FLOAT_TSDFIELD(cargo[5].paid),
	INT32_TSDFIELD(cargo[5].contents.item),
	FLOAT_TSDFIELD(cargo[5].contents.qty),
	UINT32_TSDFIELD(cargo[5].origin),
	UINT32_TSDFIELD(cargo[5].dest),
	INT32_TSDFIELD(cargo[5].due_date),

	FLOAT_TSDFIELD(cargo[6].paid),
	INT32_TSDFIELD(cargo[6].contents.item),
	FLOAT_TSDFIELD(cargo[6].contents.qty),
	UINT32_TSDFIELD(cargo[6].origin),
	UINT32_TSDFIELD(cargo[6].dest),
	INT32_TSDFIELD(cargo[6].due_date),

	FLOAT_TSDFIELD(cargo[7].paid),
	INT32_TSDFIELD(cargo[7].contents.item),
	FLOAT_TSDFIELD(cargo[7].contents.qty),
	UINT32_TSDFIELD(cargo[7].origin),
	UINT32_TSDFIELD(cargo[7].dest),
	INT32_TSDFIELD(cargo[7].due_date),

	INT32_TSDFIELD(ncargo_bays),
	FLOAT_TSDFIELD(wallet),
	FLOAT_TSDFIELD(threat_level),
	UINT8_TSDFIELD(docking_magnets),
	UINT8_TSDFIELD(passenger_berths),
	UINT8_TSDFIELD(mining_bots),
	STRING_TSDFIELD(mining_bot_name),
	STRING_FIELD(sdata.name),
	UINT16_FIELD(sdata.science_data_known),
	UINT8_FIELD(sdata.subclass),
	UINT8_FIELD(sdata.shield_strength),
	UINT8_FIELD(sdata.shield_wavelength),
	UINT8_FIELD(sdata.shield_width),
	UINT8_FIELD(sdata.shield_depth),
	UINT8_FIELD(sdata.faction),
	FLOAT_FIELD(orientation.vec[0]),
	FLOAT_FIELD(orientation.vec[1]),
	FLOAT_FIELD(orientation.vec[2]),
	FLOAT_FIELD(orientation.vec[3]),
#ifdef INCLUDE_BRIDGE_INFO_FIELDS
	INT32_BRIDGE_FIELD(initialized),
#endif
	{ 0, 0, 0, 0 },
};

#endif

