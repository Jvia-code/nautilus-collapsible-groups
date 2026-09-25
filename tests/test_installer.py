import importlib.util,pathlib,tempfile,unittest,json,subprocess
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('installer',root/'installer.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
class InstallerTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.home=pathlib.Path(self.tmp.name)/'Jean avec espaces';self.home.mkdir();self.paths=mod.locations(self.home)
  self.stage=pathlib.Path(self.tmp.name)/'stage';(self.stage/'bin').mkdir(parents=True);(self.stage/'bin/nautilus').write_text('test binary\n')
 def tearDown(self):self.tmp.cleanup()
 def test_install_uninstall_preserves_unrelated_files(self):
  other=self.home/'document-important';other.write_text('keep')
  mod.publish(self.stage,self.paths)
  self.assertTrue((self.paths['prefix']/mod.MARKER).is_file())
  self.assertTrue(self.paths['launcher'].stat().st_mode & 0o100)
  subprocess.run(['bash','-n',self.paths['launcher']],check=True)
  mod.uninstall_paths(self.paths)
  for n in ('prefix','launcher','desktop'):self.assertFalse(self.paths[n].exists())
  self.assertEqual(other.read_text(),'keep')
 def test_existing_launcher_not_overwritten(self):
  p=self.paths['launcher'];p.parent.mkdir(parents=True);p.write_text('user launcher')
  with self.assertRaises(RuntimeError):mod.publish(self.stage,self.paths)
  self.assertEqual(p.read_text(),'user launcher');self.assertFalse(self.paths['prefix'].exists())
 def test_modified_launcher_blocks_all_removal(self):
  mod.publish(self.stage,self.paths);self.paths['launcher'].write_text('changed by user')
  with self.assertRaises(RuntimeError):mod.uninstall_paths(self.paths)
  self.assertTrue(self.paths['prefix'].exists());self.assertTrue(self.paths['desktop'].exists())
 def test_missing_marker_blocks_removal(self):
  self.paths['prefix'].mkdir(parents=True)
  with self.assertRaises(RuntimeError):mod.uninstall_paths(self.paths)
  self.assertTrue(self.paths['prefix'].exists())
 def test_symlink_prefix_blocks_removal(self):
  target=pathlib.Path(self.tmp.name)/'other';target.mkdir()
  self.paths['prefix'].parent.mkdir(parents=True);self.paths['prefix'].symlink_to(target,target_is_directory=True)
  with self.assertRaises(RuntimeError):mod.uninstall_paths(self.paths)
  self.assertTrue(target.exists())
 def test_no_desktop_association_or_dbus_activation(self):
  text=mod.desktop_text(self.paths)
  self.assertNotIn('MimeType=',text);self.assertIn('DBusActivatable=false',text)
  self.assertIn('Exec="',text);self.assertIn(' --new-window %U',text)
if __name__=='__main__':unittest.main(verbosity=2)
