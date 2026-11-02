from setuptools import setup

setup(name='cbpi4-ESP32Controller',
      version='0.0.4',
      description='CraftBeerPi 4 plugin for the SammyBrew ESP32 controller',
      author='Anders Samuelsen',
      url='https://github.com/sammynorway/ESP32-SammyBrew-controller',
      license='MIT',
      include_package_data=True,
      package_data={
        # If any package contains *.txt or *.rst files, include them:
      '': ['*.txt', '*.rst', '*.yaml'],
      'cbpi4-ESP32Controller': ['*','*.txt', '*.rst', '*.yaml']},
      packages=['cbpi4-ESP32Controller'],
     )
