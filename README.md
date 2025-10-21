# HouseDCC

A web service to issue commands to model trains.

## Overview

This service communicates with trains using DCC to issue commands: movement orders and accessory controls.

> This is a work in progress that is one piece of a larger project.

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

## Web API

```
/dcc/gpio?a=NUMBER[&b=NUMBER]
```

Set which GPIO pins will be used to transmit the DCC signal. If pin B is provided, its output will reflect the opposite value of pin A. This matches how most motor controls circuits used as signal injector work: (0, 0) is no power, (1, 0) is positive voltage and (0, 1) is negative voltage.

```
/dcc/fleet/status[?known=NUMBER]
```

Return the current list of vehicles, with their speed and accessories state. A `train.layout` item allows the client to select which HouseDCC service to interact with if there are multiple instances.

The response includes the ID of the latest change. This ID is a number that changes whenever the status or the configuration changes. There is no other semantic to the ID value.

If the `known` parameter is provided, and its value is the same as the current value of the ID of the latest change, then an HTTP code 304 (not modified) is returned instead of the normal response. This is used as a way to save processing on both sides: if nothing has changed the server is not forced to dump its current status, the response is small and the client does not need to refresh data for no reason.

```
/dcc/fleet/move?id=STRING&speed=INTEGER
```

Control the movement of a locomotive or train. A negative speed means reverse direction.

```
/dcc/fleet/set?id=STRING&device=STRING&state=ON|OFF
```

Set the state of a vehicle's function device.

```
/dcc/fleet/stop[?id=STRING][&urgent=0|1]
```

Stop the designated vehicle or train. If the id is not present, stop all vehicles (see the DCC command STOP ALL). If urgent is 1 (true), the stop is immediat. If urgent is 0 or not present, this is a normal stop (it follows the breaking curve).

```
/dcc/fleet/vehicle/model?model=STRING&type=STRING[&devices=STRING:INTEGER[+STRING:INTEGER..]
```

Declare a new vehicle model, with an optional list of devices.

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

