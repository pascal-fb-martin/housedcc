# HouseDCC

A web service to issue commands to model trains.

## Overview

This service communicates with trains using DCC to issue commands: movement orders and accessory controls.

> This is a work in progress that is one piece of a larger project. See the [HouseRail](https://github.com/pascal-fb-martin/houserail) project.

DCC is the dominant standard used to communicate with model trains. This service accepts generic train commands (vehicle speed and direction control, vehicle device control, and accessory controls) and converts them into data messages conform to the DCC standard. These DCC packets are then submitted to a separate program running locally, [PiDCC](https://github.com/pascal-fb-martin/pidcc), which generates the wave encoding to be injected into the layout's power supply.

HouseDCC is responsible for formatting the DCC message's binary data, while PiDCC is responsible for all aspects of DCC transmission modulation (including CRC byte) and timing.

A complete chain of command is as follow:

- A traffic control system issue train and signaling commands (move, lights) according to the location of trains on the layout or user actions. These command are submitted using a generic train web API, with no explicit DCC dependencies.
- HouseDCC accepts these generic web commands, translates them into specific DCC messages and submits these messages to PiDCC (through a local pipe).
- PiDCC generates the PWM wave form conform to the DCC standard and uses it to control the GPIO pins attached to a power booster.
- A power booster (typically a DC motor control electronic circuit) generates the modulated 12 volt signal that both provides the traction power and serves as the communication signal.

The features of, and the commands accepted by, HouseDCC are basic: the design calls for the traffic control system to determine which trains should make which moves. The brain is in this traffic control system: HouseDCC is a converter that relieves the traffic control system from managing DCC details.

> There are two interfaces implemented by this service: control of vehicles (the fleet interface) and control of the signaling system (the signal interface). The signal interface is a future interface, most likely will be an extension of the control interface.

## Installation

* Install the OpenSSL development package(s).
* Install [echttp](https://github.com/pascal-fb-martin/echttp).
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal).
* Install [PiDCC](https://github.com/pascal-fb-martin/pidcc).
* Clone this GitHub repository.
* make
* sudo make install, or
* make debian-package and install the generated package.

## Configuration

All configuration is stored in the HouseDepot service. The HouseService group name is used to identify the layout. This means that each instance of HouseDCC only handle a single layout.

The full configuration is actually split into two parts: static configuration and state. The static configuration contains items that reflect permanent user data, typically the list of vehicle models and vehicles. The state contains items that may change more frequently, including changed initiated by the service itself or from another service, like the list of consists.

## Speed Scale

Model railroads are sized at a factional scale of the real thing (the "prototype" railroad). For example the N scale is at an approximate 1/160 the size of real railroad equipment. The DCC standard defines speed as a set of arbitrary steps. The actual locomotive speed associated with each step is defined by the locomotive's DCC decoder (and adjustable in CVs) and probably influenced by the feed's voltage provided on the layout.

This service is designed to provide a uniform API that is independent of the scale of the locomotives. The API is also intended to facilitate the implementation of a traffic control system that emulates a real railroad CTC, i.e. use the prototype distances and speeds. This led to a web API where the speed parameter is expected to be the actual speed value, not the DCC step. It is possible to define the association between DCC speed steps and actual speeds when configuring a locomotive model.

This leaves the issues of which scale and unit system to use. Any choice is valid as long as it is consistent the layout configuration. Here are two reasonable choices:

* Select the prototype scale (i.e. the speed that the real railroad would run at) and the unit system used by the prototype railroad. For example the N scale is 1/160, so all speeds would be the physical speed of the model locomotive multiplied by 160.

* Select the model scale, i.e. the physical speed the model will run at, and the unit system used at your location.

In some cases a layout may use a variable scale depending on the track location. For example the scale could be smaller between stations than within a station, to simulate greater distances between stations while keeping the layout compact. If one wants to keep the train speed consistent with the scale at each location, the scale variability would have to be handled by the client. That might be complicated and somewhat boring. Who wants to wait a few hours for the train to reach the next station? In that case, it is probably better to use the prototype scale (typically used for the stations) for the whole layout.

## Web API

```
/dcc/gpio?a=NUMBER[&b=NUMBER]
```

Set which GPIO pins will be used to transmit the DCC signal. If pin B is provided, its output will reflect the opposite value of pin A. This matches how most motor controls circuits used as signal injector work: (0, 0) is no power, (1, 0) is positive voltage and (0, 1) is negative voltage.

```
/dcc/fleet/status[?known=NUMBER][&layout=STRING]
```

Return the current list of vehicles and trains, with their speed, speed table and accessories state. A `train.layout` item allows the client to select which HouseDCC service to interact with if there are multiple instances.

The response includes the ID of the latest change. This ID is a number that changes whenever the status or the configuration changes. There is no other semantic to the ID value.

If the `known` parameter is provided, and its value is the same as the current value of the ID of the latest change, then an HTTP code 304 (not modified) is returned instead of the normal response. This is used as a way to save processing on both sides: if nothing has changed the server is not forced to dump its current status, the response is small and the client does not need to refresh data for no reason.

If the `layout` parameter is provided, the service responds with HTTP status 421 if its actual layout does not match the requested one. This option allows for an optimized discovery of the service managing a specific layout. This minimizes the discovery overhead because the non-matching services do not need to build a JSON response and the client only needs to decode a JSON response when it found the matching service.
```
/dcc/fleet/move?id=STRING&speed=INTEGER
```

Control the movement of a locomotive or train. A negative speed means reverse direction. The speed value represents the _prototype_ speed, i.e. the speed that the train would have at full scale. By convention (decided in the model's configuration), this is typically a speed in Km/h or Mph.

The speed value used must map to a DCC speed step in the speed table of this locomotive's model configuration. See the `/dcc/fleet/vehicle/model` and `/dcc/fleet/status` endpoints.

```
/dcc/fleet/set?id=STRING&device=STRING&state=ON|OFF
```

Set the state of a vehicle's function device.

```
/dcc/fleet/stop[?id=STRING][&urgent=0|1]
```

Stop the designated vehicle or train. If the id is not present, stop all vehicles (see the DCC command STOP ALL). If urgent is 1 (true), the stop is immediat. If urgent is 0 or not present, this is a normal stop (it follows the breaking curve).

```
/dcc/fleet/vehicle/model?model=STRING&type=STRING[&devices=STRING:INTEGER[+STRING:INTEGER..]][&speeds=INTEGER[+INTEGER..]]
```

Declare a new vehicle model, with an optional list of devices and speed steps. The index of each speed value in the speed list matches the step number as defined in the DCC standard (and _not_ the binary code value in the DCC message).

```
/dcc/fleet/vehicle/add?id=STRING&model=STRING&adr=INTEGER
```

Declare a new vehicle. `adr` is the DCC address.

```
/dcc/fleet/vehicle/delete?id=STRING
```

Delete an existing vehicle or model. If the same ID is used for both a vehicle and a model, the vehicle is deleted.

```
/dcc/fleet/consist/add
/dcc/fleet/consist/assign
/dcc/fleet/consist/remove
/dcc/fleet/consist/delete
```

Manage a DCC consist. (Work in progress.)

```
/dcc/fleet/config[?known=NUMBER]
```

Query the current configuration. The optional `known` parameter has the same semantic as for the `/dcc/fleet/status` endpoint.

## Configuration

The list of known DCC vehicles (locomotives and cars) can be edited from the HouseDCC web interface.

