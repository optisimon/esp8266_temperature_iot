#!/usr/bin/env python3

import unittest
import requests
import os
import json

ip = os.getenv("TARGET_IP")


class Sensors(unittest.TestCase):
    def test_reports_6_ntc_sensors(self):
        r = requests.get("http://%s/api/sensors" % ip)
#        print("STATUS: %s" % r.status_code)
#        print("HEADERS: %s" % r.headers)
#        print("TEXT: %s" % r.text)
#        print("URL: %s" % r.url)
        self.assertEqual(200, r.status_code)
        self.assertEqual("application/javascript", r.headers['content-type'])

        j = r.json()

        self.assertTrue("max_num_active" in j)
        self.assertTrue("sensors" in j)
        self.assertTrue(len(j["sensors"]) >= 6)

        num_ntc_sensors = 0
        set_of_ids = set()
        for s in j["sensors"]:
            # all required fields present
            required_fields = ("id", "type", "name", "active", "lastValue")
            self.assertTrue(all([x in s for x in required_fields]))

            # id numbers should not be repeated
            self.assertTrue(type(s["id"]) == str)
            self.assertFalse(s["id"] in set_of_ids)
            set_of_ids.add(s["id"])

            # active should only be 0 or 1
            self.assertTrue(type(s["active"]) == int)
            self.assertTrue(s["active"] == 0 or s["active"] == 1)

            # summing up number of ntc sensors
            self.assertTrue(type(s["type"]) == str)
            if s["type"] == "NTC":
                num_ntc_sensors += 1

            self.assertTrue(type(s["name"]) == str)
            self.assertTrue(type(s["lastValue"]) == float)

        self.assertEqual(6, num_ntc_sensors)

        # ensure indivivual sensors can be queried separately as well
        for s in j["sensors"]:
            r2 = requests.get("http://%s/api/sensors/%s" % (ip, s["id"]))

            self.assertEqual(200, r2.status_code)
            j2 = r2.json()

            # all required fields present
            required_fields = ("id", "type", "name", "active", "lastValue")
            self.assertTrue(all([x in j2 for x in required_fields]))

            # id numbers should match
            self.assertEqual(str, type(j2["id"]))
            self.assertEqual(s["id"], j2["id"])

            # active should match
            self.assertEqual(int, type(s["active"]))
            self.assertEqual(s["active"], j2["active"])

            # summing up number of ntc sensors
            self.assertEqual(str, type(s["type"]))
            self.assertEqual(str, type(s["name"]))
            self.assertEqual(float, type(s["lastValue"]))

            self.assertEqual(s["type"], j2["type"])
            self.assertEqual(s["name"], j2["name"])
            self.assertEqual(s["lastValue"], j2["lastValue"])  # TODO: keep this? (will fail intermitently)


class Readings(unittest.TestCase):
    def test_readings_1h(self):
        r = requests.get("http://%s/api/readings/1h" % ip)
        self.assertEqual(200, r.status_code)
        self.assertEqual("application/javascript", r.headers['content-type'])

        j = r.json()

        self.assertTrue("sensors" in j)
        self.assertTrue(len(j["sensors"]) >= 1)  # TODO: should we set it up to use 6 channels always??

        set_of_ids = set()
        for s in j["sensors"]:
            # all required fields present
            required_fields = ("id", "type", "name", "readings")
            self.assertTrue(all([x in s for x in required_fields]))

            # id numbers should not be repeated
            self.assertEqual(str, type(s["id"]))
            self.assertFalse(s["id"] in set_of_ids)
            set_of_ids.add(s["id"])

            self.assertEqual(str, type(s["type"]))
            self.assertTrue(s["type"] == "NTC" or s["type"] == "OneWire")

            self.assertEqual(str, type(s["name"]))

            # Hour readings should have 360 floats
            self.assertEqual(360, len(s["readings"]))
            for val in s["readings"]:
                self.assertEqual(float, type(val))
                self.assertTrue(val >= -100 and val <= 120)

    def test_readings_24h(self):
        r = requests.get("http://%s/api/readings/24h" % ip)
        self.assertEqual(200, r.status_code)
        self.assertEqual("application/javascript", r.headers['content-type'])

        j = r.json()

        self.assertTrue("sensors" in j)
        self.assertTrue(len(j["sensors"]) >= 1)  # TODO: should we set it up to use 6 channels always??

        set_of_ids = set()
        for s in j["sensors"]:
            # all required fields present
            required_fields = ("id", "type", "name", "readings")
            self.assertTrue(all([x in s for x in required_fields]))

            # id numbers should not be repeated
            self.assertEqual(str, type(s["id"]))
            self.assertFalse(s["id"] in set_of_ids)
            set_of_ids.add(s["id"])

            self.assertEqual(str, type(s["type"]))
            self.assertTrue(s["type"] == "NTC" or s["type"] == "OneWire")

            self.assertEqual(str, type(s["name"]))

            # Hour readings should have 1440 floats
            self.assertEqual(1440, len(s["readings"]))
            for val in s["readings"]:
                self.assertEqual(float, type(val))
                self.assertTrue(val >= -100 and val <= 120)


class SoftAP(unittest.TestCase):
    def test_real_password_not_returned(self):
        r = requests.get("http://%s/api/wifi/softap" % ip)
        self.assertEqual(200, r.status_code)
        self.assertEqual("application/javascript", r.headers['content-type'])

        j = r.json()
        self.assertTrue("password" in j)
        self.assertEqual("********", j["password"])

    def test_required_fields_present(self):
            r = requests.get("http://%s/api/wifi/softap" % ip)
            self.assertEqual(200, r.status_code)
            self.assertEqual("application/javascript", r.headers['content-type'])

            j = r.json()

            # all required fields present
            required_fields = ("ssid", "password", "ip", "gateway", "subnet")
            self.assertTrue(all([x in j for x in required_fields]))


class Network(unittest.TestCase):
    def test_real_password_not_returned(self):
        r = requests.get("http://%s/api/wifi/network" % ip)
        self.assertEqual(200, r.status_code)
        self.assertEqual("application/javascript", r.headers['content-type'])

        j = r.json()
        self.assertTrue("password" in j)
        self.assertEqual("********", j["password"])

    def test_required_fields_present(self):
        r = requests.get("http://%s/api/wifi/network" % ip)
        self.assertEqual(200, r.status_code)
        self.assertEqual("application/javascript", r.headers['content-type'])

        j = r.json()

        # all required fields present
        required_fields = (
            "enabled",
            "assignment",
            "ssid",
            "password",
            "static")
        self.assertTrue(all([x in j for x in required_fields]))
        self.assertEqual(int, type(j["enabled"]))
        self.assertTrue(j["enabled"] == 0 or j["enabled"] == 1)

        self.assertEqual(str, type(j["assignment"]))
        self.assertTrue(j["assignment"] in ("dhcp", "static"))

        self.assertEqual(str, type(j["ssid"]))
        self.assertEqual(str, type(j["password"]))

        self.assertTrue(all([x in j["static"] for x in ("ip", "gateway", "subnet")]))
        self.assertEqual(str, type(j["static"]["ip"]))
        self.assertEqual(str, type(j["static"]["gateway"]))
        self.assertEqual(str, type(j["static"]["subnet"]))


if __name__ == "__main__":
    unittest.main()
