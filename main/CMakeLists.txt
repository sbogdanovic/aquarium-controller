idf_component_register(
	SRCS "main.c"
	INCLUDE_DIRS "."
	REQUIRES controller_core water_level_sensor_component water_pump controller_indicator doser
)
