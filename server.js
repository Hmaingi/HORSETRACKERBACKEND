const express = require('express');
const admin = require('firebase-admin');
const bodyParser = require('body-parser');
const cors = require('cors');
const app = express();
const PORT = process.env.PORT || 3002;
require('dotenv').config();
admin.initializeApp({
  credential: admin.credential.cert({
    projectId: process.env.FIREBASE_PROJECT_ID,
    privateKey: process.env.FIREBASE_PRIVATE_KEY.replace(/\\n/g, '\n'), // Replace escaped newlines
    clientEmail: process.env.FIREBASE_CLIENT_EMAIL,
  }),
  databaseURL: 'https://horsetracker-aab6c.firebaseio.com'
});

const db = admin.firestore();

// Middleware
app.use(cors());
app.use(bodyParser.json());

// Health check endpoint
app.get('/', (req, res) => {
  res.send('Horse Tracker API Server');
});

app.post('/api/data', async (req, res) => {
  try {
    const sensorData = req.body;
    
    // Extract deviceId from the request body
    const deviceId = sensorData.deviceId;
    if (!deviceId) {
      return res.status(400).json({ error: 'Device ID required' });
    }

    // Check if device exists in Firestore
    const deviceRef = db.collection('devices').doc(deviceId);
    const deviceDoc = await deviceRef.get();
    
    let deviceData;
    if (!deviceDoc.exists) {
      // Create new device with default values
      deviceData = {
        deviceId: deviceId,
        isAssigned: false,
        assignedHorseId: null,
        createdAt: admin.firestore.FieldValue.serverTimestamp()
      };
      await deviceRef.set(deviceData);
      console.log(`Created new device: ${deviceId}`);
    } else {
      deviceData = deviceDoc.data();
    }
    
    // Generate new horse ID if device is not assigned
    let horseId = deviceData.assignedHorseId;
    if (!deviceData.isAssigned) {
      // Get all horses to determine next ID
      const horsesSnapshot = await db.collection('horses').get();
      const horseCount = horsesSnapshot.size;
      horseId = `horse${String(horseCount + 1).padStart(3, '0')}`;
      
      // Update device with new horse ID
      await deviceRef.update({
        assignedHorseId: horseId,
        isAssigned: false // Will be set to true via UI
      });
    }
    
    // Prepare update data
    const updateData = {
      heartRate: sensorData.heartRate,
      temperature: sensorData.temperature,
      oxygenSaturation: sensorData.oxygen,
      speed: sensorData.speed,
      coordinates: sensorData.gps ? {
        lat: sensorData.gps.lat,
        lng: sensorData.gps.lng
      } : null,
      lastUpdated: admin.firestore.FieldValue.serverTimestamp(),
      status: calculateStatus(sensorData),
      deviceId: deviceId
    };
    
    // Clean undefined values
    Object.keys(updateData).forEach(key => updateData[key] === undefined && delete updateData[key]);
    
    // Update or create horse document
    const horseRef = db.collection('horses').doc(horseId);
    const horseDoc = await horseRef.get();
    
    if (horseDoc.exists) {
      await horseRef.update(updateData);
    } else {
      await horseRef.set({
        ...updateData,
        horseId: horseId,
        createdAt: admin.firestore.FieldValue.serverTimestamp()
      });
    }
    
    res.status(200).json({ success: true, horseId: horseId });
  } catch (error) {
    console.error('Data submission error:', error);
    res.status(500).json({ error: 'Internal server error' });
  }
});

// Updated endpoint for admin dashboard to complete horse assignment
app.post('/api/assign-horse', async (req, res) => {
  console.log('Request body:', req.body);
  try {
    const { deviceId, horseDetails } = req.body;
    if (!deviceId) {
      console.log('Missing deviceId');
      return res.status(400).json({ error: 'Device ID required' });
    }
    const deviceRef = db.collection('devices').doc(deviceId);
    const deviceDoc = await deviceRef.get();
    if (!deviceDoc.exists) {
      console.log(`Device ${deviceId} not found`);
      return res.status(404).json({ error: 'Device not found' });
    }
    const deviceData = deviceDoc.data();
    if (deviceData.isAssigned) {
      console.log(`Device ${deviceId} already assigned`);
      return res.status(400).json({ error: 'Device is already assigned' });
    }
    const horseId = deviceData.assignedHorseId;
    if (!horseId) {
      console.log(`No horseId for device ${deviceId}`);
      return res.status(400).json({ error: 'No horse ID associated with this device' });
    }
    const horseRef = db.collection('horses').doc(horseId);
    const horseDoc = await horseRef.get();
    if (!horseDoc.exists) {
      console.log(`Horse ${horseId} not found`);
      return res.status(404).json({ error: 'Horse not found' });
    }
    const horseData = horseDoc.data();
    if (horseData.isAssigned) {
      console.log(`Horse ${horseId} already assigned`);
      return res.status(400).json({ error: 'Horse is already assigned' });
    }
    await deviceRef.update({
      isAssigned: true,
      assignedHorseId: horseId,
      lastUpdated: admin.firestore.FieldValue.serverTimestamp()
    });
    await horseRef.update({
      ...horseDetails,
      isAssigned: true,
      lastUpdated: admin.firestore.FieldValue.serverTimestamp()
    });
    console.log(`Horse ${horseId} assigned successfully`);
    res.status(200).json({ success: true, message: 'Horse assignment completed', horseId });
  } catch (error) {
    console.error('Horse assignment error:', error);
    res.status(500).json({ error: 'Internal server error' });
  }
});

// Endpoint to get unassigned devices and horses
app.get('/api/unassigned', async (req, res) => {
  try {
    // Get unassigned devices
    const devicesSnapshot = await db.collection('devices')
      .where('isAssigned', '==', false)
      .get();
    
    const unassignedDevices = devicesSnapshot.docs.map(doc => ({
      deviceId: doc.data().deviceId,
      assignedHorseId: doc.data().assignedHorseId
    }));

    // Get unassigned horses
    const horsesSnapshot = await db.collection('horses')
      .where('isAssigned', '==', false)
      .get();
    
    const unassignedHorses = horsesSnapshot.docs.map(doc => ({
      horseId: doc.data().horseId,
      deviceId: doc.data().deviceId
    }));

    res.status(200).json({
      success: true,
      unassignedDevices,
      unassignedHorses
    });
  } catch (error) {
    console.error('Error fetching unassigned items:', error);
    res.status(500).json({ error: 'Internal server error' });
  }
});
// New endpoint to get all assigned horses
app.get('/api/horses', async (req, res) => {
  try {
    const horsesSnapshot = await db.collection('horses')
      .where('isAssigned', '==', true)
      .get();
    
    const horses = horsesSnapshot.docs.map(doc => {
      const data = doc.data();
      return {
        horseId: data.horseId,
        name: data.name || "Unnamed",
        heartRate: data.heartRate || 0,
        temperature: data.temperature || 0,
        oxygenSaturation: data.oxygenSaturation || 0,
        speed: data.speed || 0,
        coordinates: data.coordinates || { lat: 0, lng: 0 },
        location: data.location || "Unknown",
        lastUpdated: data.lastUpdated ? data.lastUpdated.toDate().toISOString() : "Unknown",
        status: data.status || "normal",
        behavioralInsights: data.behavioralInsights || "Not enough data",
        deviceId: data.deviceId
      };
    });

    res.status(200).json({ success: true, horses });
  } catch (error) {
    console.error('Error fetching horses:', error);
    res.status(500).json({ error: 'Internal server error' });
  }
});
// Helper function to calculate horse status
function calculateStatus(sensorData) {
  if (sensorData.heartRate > 100) return 'elevated';
  if (sensorData.temperature > 38.5) return 'fever';
  return 'normal';
}

// Start server
app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});